package xai.client.module;

import xai.client.backend.SocketServer;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import net.minecraft.client.Minecraft;
import net.minecraft.client.DeltaTracker;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;

import java.io.IOException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public class ESP {
    private static final ESP INSTANCE = new ESP();
    private Method getFovMethod;
    private long lastTime = 0;

    public static ESP getInstance() {
        return INSTANCE;
    }
    
    public void start() {
        WorldRenderEvents.END.register(this::update);
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

            // Collect entities on Main Thread
            List<Entity> entities = new ArrayList<>();
            for (Entity e : client.level.entitiesForRendering()) {
                entities.add(e);
            }

            // Send Data via SocketServer
            SocketServer.getInstance().sendData(out -> {
                try {
                    int entityCount = 0;
                    for (Entity e : entities) {
                        if (e instanceof Player && e != localPlayer) entityCount++;
                    }

                    out.writeInt(0xCAFEBABE);
                    
                    out.writeFloat(camYaw);
                    out.writeFloat(camPitch);
                    
                    out.writeDouble(camPos.x);
                    out.writeDouble(camPos.y);
                    out.writeDouble(camPos.z);
                    
                    out.writeFloat((float) finalFov);
                    
                    out.writeBoolean(screenOpen);
                    
                    out.writeInt(entityCount);

                    for (Entity entity : entities) {
                        if (entity instanceof Player && entity != localPlayer) {
                            Player player = (Player) entity;
                            double x = entity.xo + (entity.getX() - entity.xo) * tickDelta;
                            double y = entity.yo + (entity.getY() - entity.yo) * tickDelta;
                            double z = entity.zo + (entity.getZ() - entity.zo) * tickDelta;

                            float relX = (float)(x - camPos.x);
                            float relY = (float)(y - camPos.y);
                            float relZ = (float)(z - camPos.z);

                            out.writeInt(entity.getId());
                            out.writeFloat(relX);
                            out.writeFloat(relY);
                            out.writeFloat(relZ);
                            out.writeFloat(entity.getBbWidth());
                            out.writeFloat(entity.getBbHeight());

                            // Use Nametags module to write the rest
                            Nametags.writeData(out, player, client);
                        }
                    }
                } catch (IOException e) {
                    e.printStackTrace();
                }
            });

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
