package xai.client.module;

import xai.client.backend.SocketServer;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.client.DeltaTracker;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;

import java.io.IOException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

public class ESP {
    private static final ESP INSTANCE = new ESP();
    private Method getFovMethod;
    private long lastTime = 0;
    private final java.util.Map<Integer, Long> sentMobs = new java.util.concurrent.ConcurrentHashMap<>();
    private long lastCleanup = 0;
    private long lastDebugTime = 0;
    private int frames = 0;
    private long totalCollectTime = 0;
    private long totalSerializeTime = 0;
    
    // Concurrency Control
    private final AtomicBoolean isProcessing = new AtomicBoolean(false);

    public static ESP getInstance() {
        return INSTANCE;
    }
    
    public void start() {
        WorldRenderEvents.END.register(this::update);
        SocketServer.getInstance().addConnectionListener(sentMobs::clear);
    }

    public void update(WorldRenderContext context) {
        long now = System.currentTimeMillis();
        if (now - lastTime < 7) return; // Cap at ~144hz
        lastTime = now;

        Minecraft client = Minecraft.getInstance();
        if (client.level == null || client.player == null) return;
        
        DeltaTracker tracker = client.getDeltaTracker();

        try {
            float tickDelta = tracker.getGameTimeDeltaPartialTick(false);
            
            Vec3 camPos = client.gameRenderer.getMainCamera().getPosition();
            float camYaw = client.gameRenderer.getMainCamera().getYRot();
            float camPitch = client.gameRenderer.getMainCamera().getXRot();
            
            if (getFovMethod == null) {
                try {
                    for (Method m : client.gameRenderer.getClass().getDeclaredMethods()) {
                        if ((m.getReturnType() == double.class || m.getReturnType() == float.class) && m.getParameterCount() == 3) {
                            Class<?>[] params = m.getParameterTypes();
                            if (net.minecraft.client.Camera.class.isAssignableFrom(params[0]) &&
                                params[1] == float.class &&
                                params[2] == boolean.class) {
                                m.setAccessible(true);
                                getFovMethod = m;
                                break;
                            }
                        }
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }

            double fov = 70.0;
            if (getFovMethod != null) {
                try {
                    Object result = getFovMethod.invoke(client.gameRenderer, client.gameRenderer.getMainCamera(), tickDelta, true);
                    if (result instanceof Double) {
                        fov = (double) result;
                    } else if (result instanceof Float) {
                        fov = (double) (float) result;
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
            
            final double finalFov = fov;
            final boolean screenOpen = client.screen != null;
            final Player localPlayer = client.player;

            long t0 = System.nanoTime();

            // Collect entities on Main Thread
            List<Entity> entities = new ArrayList<>();
            
            boolean masterEspEnabled = SocketServer.getInstance().isModuleEnabled("ESP");
            boolean showGeneric = masterEspEnabled && SocketServer.getInstance().isShowGenericMobs();
            boolean showAll = masterEspEnabled && SocketServer.getInstance().isShowAllEntities();
            Set<String> specificMobs = SocketServer.getInstance().getSpecificMobs();
            boolean hasSpecific = masterEspEnabled && !specificMobs.isEmpty();

            boolean showPlayers = SocketServer.getInstance().isModuleEnabled("PlayerESP") || SocketServer.getInstance().isModuleEnabled("Nametags");
            
            // Periodic Cleanup of Sent Mobs Cache (Every 1s)
            if (now - lastCleanup > 1000) {
                lastCleanup = now;
                sentMobs.entrySet().removeIf(entry -> now - entry.getValue() > 5000); // Remove if not seen for 5s
            }

            for (Entity e : client.level.entitiesForRendering()) {
                if (e == localPlayer) continue;
                
                boolean added = false;

                // 1. Players
                if (e instanceof Player) {
                    if (showPlayers) {
                        entities.add(e);
                        added = true;
                    }
                }
                
                if (added) continue;

                // 2. Specific Mobs (High Priority)
                if (hasSpecific) {
                    String name = e.getType().getDescription().getString();
                    if (specificMobs.contains(name)) {
                        entities.add(e);
                        added = true;
                    }
                }

                if (added) continue;

                // 3. Generic Mobs (Living, Non-Player)
                if (showGeneric && e instanceof LivingEntity && !(e instanceof Player)) {
                    entities.add(e);
                    added = true;
                }

                if (added) continue;

                // 4. All Entities
                if (showAll) {
                    entities.add(e);
                    added = true;
                }
            }
            
            long t1 = System.nanoTime();

                // Check Flow Control
                if (!SocketServer.getInstance().hasClients()) return;
                if (isProcessing.get()) return; // Drop frame if busy
                isProcessing.set(true);

                // Offload Serialization to IO Thread
                SocketServer.getInstance().sendData((out) -> {
                    long t2 = System.nanoTime();
                    try {
                    out.writeInt(0xCAFEBABE);
                    
                    out.writeFloat(camYaw);
                    out.writeFloat(camPitch);
                    
                    out.writeDouble(camPos.x);
                    out.writeDouble(camPos.y);
                    out.writeDouble(camPos.z);
                    
                    out.writeFloat((float) finalFov);
                    
                    out.writeBoolean(screenOpen);

                    int targetId = -1;
                    if (client.hitResult != null && client.hitResult.getType() == net.minecraft.world.phys.HitResult.Type.ENTITY) {
                         net.minecraft.world.phys.EntityHitResult hit = (net.minecraft.world.phys.EntityHitResult) client.hitResult;
                         if (hit.getEntity() instanceof Player) {
                             targetId = hit.getEntity().getId();
                         }
                    }
                    out.writeInt(targetId);
                    
                    out.writeInt(entities.size());

                    for (Entity entity : entities) {
                        double x = entity.xo + (entity.getX() - entity.xo) * tickDelta;
                        double y = entity.yo + (entity.getY() - entity.yo) * tickDelta;
                        double z = entity.zo + (entity.getZ() - entity.zo) * tickDelta;

                        float relX = (float)(x - camPos.x);
                        float relY = (float)(y - camPos.y);
                        float relZ = (float)(z - camPos.z);

                        if (entity instanceof Player) {
                             out.writeByte(0); // Type 0: Full Player
                             out.writeInt(entity.getId());
                             out.writeFloat(relX);
                             out.writeFloat(relY);
                             out.writeFloat(relZ);
                             out.writeFloat(entity.getBbWidth());
                             out.writeFloat(entity.getBbHeight());
                             Nametags.writeData(out, (LivingEntity)entity, client);
                        } else if (entity instanceof LivingEntity) {
                             // Mob Logic: Check if sent before
                             if (sentMobs.containsKey(entity.getId())) {
                                out.writeByte(2); // Type 2: Pos Only
                                out.writeInt(entity.getId());
                                out.writeFloat(relX);
                                out.writeFloat(relY);
                                out.writeFloat(relZ);
                                sentMobs.put(entity.getId(), now); // Update timestamp
                            } else {
                                out.writeByte(1); // Type 1: Full Mob
                                sentMobs.put(entity.getId(), now);
                                out.writeInt(entity.getId());
                                out.writeFloat(relX);
                                out.writeFloat(relY);
                                out.writeFloat(relZ);
                                 out.writeFloat(entity.getBbWidth());
                                 out.writeFloat(entity.getBbHeight());
                                 Nametags.writeData(out, (LivingEntity)entity, client);
                             }
                        } else { // Non-Living (Items, etc.) or forced via ShowAll/Specific
                             if (sentMobs.containsKey(entity.getId())) {
                                out.writeByte(2); // Type 2: Pos Only
                                out.writeInt(entity.getId());
                                out.writeFloat(relX);
                                out.writeFloat(relY);
                                out.writeFloat(relZ);
                                sentMobs.put(entity.getId(), now); 
                            } else {
                                out.writeByte(1); // Type 1: Treat as Mob for now
                                sentMobs.put(entity.getId(), now);
                                out.writeInt(entity.getId());
                                out.writeFloat(relX);
                                out.writeFloat(relY);
                                out.writeFloat(relZ);
                                out.writeFloat(entity.getBbWidth());
                                out.writeFloat(entity.getBbHeight());
                                
                                // Write dummy/basic data since Nametags.writeData expects LivingEntity
                                String name = entity.getType().getDescription().getString();
                                out.writeInt(name.length());
                                out.writeBytes(name);
                                out.writeInt(0); // Ping
                                out.writeFloat(1.0f); // Health
                                out.writeFloat(1.0f); // Max
                                out.writeFloat(0.0f); // Abs
                                for(int i=0; i<6; i++) out.writeInt(0); // Items
                             }
                        }
                    }

                    long t3 = System.nanoTime();
                    totalCollectTime += (t1 - t0);
                    totalSerializeTime += (t3 - t2);
                    frames++;

                    if (System.currentTimeMillis() - lastDebugTime > 1000) {
                        double avgCollect = (totalCollectTime / (double)frames) / 1_000_000.0;
                        double avgSerialize = (totalSerializeTime / (double)frames) / 1_000_000.0;
                        System.out.printf("[Perf] Java: Collect=%.2fms, Serialize=%.2fms, Entities=%d%n", avgCollect, avgSerialize, entities.size());
                        lastDebugTime = System.currentTimeMillis();
                        frames = 0;
                        totalCollectTime = 0;
                        totalSerializeTime = 0;
                    }

                } catch (IOException e) {
                        e.printStackTrace();
                    }
                }, () -> isProcessing.set(false)); // Release Lock
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
}
