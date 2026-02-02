package xai.client.backend;

import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.core.Holder;
import net.minecraft.world.item.enchantment.Enchantment;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.client.multiplayer.PlayerInfo;
import net.minecraft.core.registries.BuiltInRegistries;
import java.lang.reflect.Method;

import java.io.DataOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.StreamSupport;

import java.nio.charset.StandardCharsets;

public class SocketServer {
    private static final SocketServer INSTANCE = new SocketServer();
    private static final int PORT = 25566;

    private static final Map<String, String> ENCHANTMENT_ABBREVIATIONS = Map.ofEntries(
        Map.entry("protection", "PR"),
        Map.entry("fire_protection", "FP"),
        Map.entry("feather_falling", "FF"),
        Map.entry("blast_protection", "BP"),
        Map.entry("projectile_protection", "PP"),
        Map.entry("respiration", "R"),
        Map.entry("aqua_affinity", "AA"),
        Map.entry("thorns", "TH"),
        Map.entry("depth_strider", "DS"),
        Map.entry("frost_walker", "FW"),
        Map.entry("binding_curse", "CB"),
        Map.entry("sharpness", "SH"),
        Map.entry("smite", "SM"),
        Map.entry("bane_of_arthropods", "BA"),
        Map.entry("knockback", "KB"),
        Map.entry("fire_aspect", "FA"),
        Map.entry("looting", "LO"),
        Map.entry("sweeping", "SW"),
        Map.entry("efficiency", "EF"),
        Map.entry("silk_touch", "ST"),
        Map.entry("unbreaking", "UB"),
        Map.entry("fortune", "FO"),
        Map.entry("power", "PO"),
        Map.entry("punch", "PU"),
        Map.entry("flame", "FL"),
        Map.entry("infinity", "IN"),
        Map.entry("luck_of_the_sea", "LS"),
        Map.entry("lure", "LU"),
        Map.entry("loyalty", "LY"),
        Map.entry("impaling", "IM"),
        Map.entry("riptide", "RI"),
        Map.entry("channeling", "CH"),
        Map.entry("multishot", "MS"),
        Map.entry("quick_charge", "QC"),
        Map.entry("piercing", "PI"),
        Map.entry("mending", "ME"),
        Map.entry("vanishing_curse", "CV"),
        Map.entry("soul_speed", "SS"),
        Map.entry("swift_sneak", "SN")
    );

    private final List<DataOutputStream> clients = new CopyOnWriteArrayList<>();
    private final Map<String, Boolean> moduleStates = new ConcurrentHashMap<>();
    private ServerSocket serverSocket;
    private boolean running = false;
    private Method getFovMethod;

    public static SocketServer getInstance() {
        return INSTANCE;
    }

    public boolean isModuleEnabled(String name) {
        return moduleStates.getOrDefault(name, false);
    }

    public void start() {
        if (running) return;
        running = true;

        new Thread(() -> {
            try {
                serverSocket = new ServerSocket(PORT);
                System.out.println("Binary Overlay Server started on port " + PORT);

                while (running) {
                    try {
                        Socket clientSocket = serverSocket.accept();
                        clientSocket.setTcpNoDelay(true); // Critical for <16ms latency
                        
                        DataOutputStream out = new DataOutputStream(clientSocket.getOutputStream());
                        clients.add(out);
                        
                        // Start Reader Thread
                        new Thread(() -> handleClientRead(clientSocket)).start();

                        System.out.println("Overlay connected: " + clientSocket.getInetAddress());
                    } catch (SocketException e) {
                        // Socket closed gracefully (usually)
                        if (running) {
                            e.printStackTrace();
                        }
                    }
                }
            } catch (IOException e) {
                if (running) {
                    e.printStackTrace();
                }
            }
        }, "Overlay-Server-Thread").start();

        WorldRenderEvents.END.register(this::broadcastData);
    }

    private void handleClientRead(Socket socket) {
        try {
            DataInputStream in = new DataInputStream(socket.getInputStream());
            while (running && !socket.isClosed()) {
                int header = in.readInt();
                
                if (header == 0xDEADBEEF) { // Update State
                    int count = in.readInt();
                    for (int i = 0; i < count; i++) {
                        int nameLen = in.readInt();
                        byte[] nameBytes = new byte[nameLen];
                        in.readFully(nameBytes);
                        String name = new String(nameBytes);
                        boolean enabled = in.readByte() != 0;
                        moduleStates.put(name, enabled);
                    }
                } else if (header == 0xBADF00D) { // Disable Request
                    boolean fully = in.readByte() != 0;
                    System.out.println("Received Disable Request. Fully: " + fully);
                    
                    if (fully) {
                        shutdown(true);
                    } else {
                        // Just stop listening/broadcasting, but ready to restart
                        shutdown(false);
                    }
                    break;
                }
            }
        } catch (IOException e) {
            // Connection lost
        }
    }

    public boolean fullyDisabled = false;

    private void shutdown(boolean fully) {
        running = false;
        fullyDisabled = fully;
        try {
            if (serverSocket != null) {
                serverSocket.close();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        clients.clear();
        moduleStates.clear(); // Reset module states on disconnect
        System.out.println("Overlay Server Stopped. Fully: " + fully);
    }
    
    public void reactivate() {
        if (fullyDisabled) {
            System.out.println("Cannot reactivate: Fully disabled.");
            return;
        }
        if (running) return; // Already running
        start();
    }

    private long lastTime = 0;

    private void broadcastData(WorldRenderContext context) {
        if (clients.isEmpty()) return;

        // Cap at ~144 FPS (approx 7ms)
        long now = System.currentTimeMillis();
        if (now - lastTime < 7) return;
        lastTime = now;

        Minecraft client = Minecraft.getInstance();
        DeltaTracker tracker = client.getDeltaTracker();
        if (client.level == null || client.player == null) return;

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
                                System.out.println("XaiClient: Found FOV method: " + m.getName() + " Return: " + m.getReturnType().getName());
                                break;
                            }
                        }
                    }
                    if (getFovMethod == null) {
                        System.out.println("XaiClient: ERROR - Could not find getFov method via reflection!");
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

            // 1. Filter Entities
            Iterable<Entity> allEntities = client.level.entitiesForRendering();
            int entityCount = 0;
            for (Entity e : allEntities) {
                if (e instanceof Player && e != client.player) entityCount++;
            }

            // 2. Broadcast Binary Packet
            for (DataOutputStream out : clients) {
                // Header: Magic Number (0xCAFEBABE)
                out.writeInt(0xCAFEBABE);
                
                // Camera Orientation
                out.writeFloat(camYaw);
                out.writeFloat(camPitch);
                out.writeFloat((float) fov);
                
                // Screen Status
                out.writeBoolean(client.screen != null);
                
                // Entity Count
                out.writeInt(entityCount);

                // Entity List
                for (Entity entity : allEntities) {
                    if (entity instanceof Player && entity != client.player) {
                        Player player = (Player) entity;
                        // Interpolate Position
                        // MojMap: xo, yo, zo is prevX, prevY, prevZ
                        double x = entity.xo + (entity.getX() - entity.xo) * tickDelta;
                        double y = entity.yo + (entity.getY() - entity.yo) * tickDelta;
                        double z = entity.zo + (entity.getZ() - entity.zo) * tickDelta;

                        // Calculate Relative Position (World - Camera)
                        float relX = (float)(x - camPos.x);
                        float relY = (float)(y - camPos.y);
                        float relZ = (float)(z - camPos.z);

                        out.writeInt(entity.getId());
                        out.writeFloat(relX);
                        out.writeFloat(relY);
                        out.writeFloat(relZ);
                        out.writeFloat(entity.getBbWidth());
                        out.writeFloat(entity.getBbHeight());

                        // --- NAMETAG DATA ---
                        // Name
                        String name = player.getName().getString();
                        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
                        out.writeInt(nameBytes.length);
                        out.write(nameBytes);

                        // Ping
                        int ping = -1;
                        if (client.getConnection() != null) {
                            PlayerInfo info = client.getConnection().getPlayerInfo(player.getUUID());
                            if (info != null) ping = info.getLatency();
                        }
                        out.writeInt(ping);

                        // Health
                        out.writeFloat(player.getHealth());
                        out.writeFloat(player.getMaxHealth());
                        out.writeFloat(player.getAbsorptionAmount());

                        // Items
                        // Order: Main, Off, Head, Chest, Legs, Feet
                        ItemStack[] items = new ItemStack[] {
                            player.getMainHandItem(),
                            player.getOffhandItem(),
                            player.getItemBySlot(EquipmentSlot.HEAD),
                            player.getItemBySlot(EquipmentSlot.CHEST),
                            player.getItemBySlot(EquipmentSlot.LEGS),
                            player.getItemBySlot(EquipmentSlot.FEET)
                        };

                        for (ItemStack stack : items) {
                            if (stack.isEmpty()) {
                                out.writeInt(0); // Name length 0 -> Empty
                            } else {
                                String itemId = BuiltInRegistries.ITEM.getKey(stack.getItem()).getPath();
                                byte[] idBytes = itemId.getBytes(StandardCharsets.UTF_8);
                                out.writeInt(idBytes.length);
                                out.write(idBytes);
                                out.writeInt(stack.getCount());
                                
                                // Durability
                                if (stack.isDamageableItem()) {
                                    out.writeInt(stack.getMaxDamage());
                                    out.writeInt(stack.getDamageValue());
                                } else {
                                    out.writeInt(0); // Max Damage
                                    out.writeInt(0); // Damage
                                }

                                // Enchantments
                                ItemEnchantments enchants = stack.getEnchantments();
                                var entrySet = enchants.entrySet();
                                out.writeInt(entrySet.size());
                                
                                for (var entry : entrySet) {
                                    Holder<Enchantment> holder = entry.getKey();
                                    int level = entry.getIntValue();
                                    String enchName = holder.unwrapKey().get().location().getPath();
                                    String abbr = ENCHANTMENT_ABBREVIATIONS.getOrDefault(enchName, enchName.substring(0, Math.min(2, enchName.length())).toUpperCase());
                                    
                                    byte[] abbrBytes = abbr.getBytes(StandardCharsets.UTF_8);
                                    out.writeInt(abbrBytes.length);
                                    out.write(abbrBytes);
                                    out.writeInt(level);
                                }
                            }
                        }
                    }
                }
                out.flush();
            }
            
            // Cleanup disconnected clients
            clients.removeIf(out -> {
                try {
                    out.flush();
                    return false;
                } catch (IOException e) {
                    return true;
                }
            });

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
