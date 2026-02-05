package xai.client.backend;

import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientChunkEvents;
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
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.ChunkPos;

import java.lang.reflect.Method;
import java.io.DataOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.ArrayList;
import java.util.HashMap;
import java.nio.charset.StandardCharsets;
import net.minecraft.world.level.Level;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class SocketServer {
    private static final SocketServer INSTANCE = new SocketServer();
    private static final int PORT = 25566;

    private ExecutorService scanExecutor;
    private ExecutorService networkExecutor;

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
    private final Set<String> wantedBlocks = ConcurrentHashMap.newKeySet();
    
    // Persistent state for differential updates
    private final Map<BlockPos, String> knownBlocks = new ConcurrentHashMap<>();
    // Cache per chunk for fast unload
    private final Map<ChunkPos, List<BlockPos>> chunkCache = new ConcurrentHashMap<>();
    
    private ServerSocket serverSocket;
    private boolean running = false;
    private Method getFovMethod;

    public static class FoundBlock {
        public String id;
        public int x, y, z;
        public FoundBlock(String id, int x, int y, int z) {
            this.id = id; this.x = x; this.y = y; this.z = z;
        }
    }

    public static SocketServer getInstance() {
        return INSTANCE;
    }

    public boolean isModuleEnabled(String name) {
        return moduleStates.getOrDefault(name, false);
    }

    public void start() {
        if (running) return;
        running = true;

        scanExecutor = Executors.newSingleThreadExecutor(r -> new Thread(r, "Xai-Scan-Thread"));
        networkExecutor = Executors.newSingleThreadExecutor(r -> new Thread(r, "Xai-Network-Thread"));

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
                        
                        // Send full state on connect
                        networkExecutor.submit(() -> sendFullBlockState(out));

                        // Start Reader Thread
                        new Thread(() -> handleClientRead(clientSocket)).start();

                        System.out.println("Overlay connected: " + clientSocket.getInetAddress());
                    } catch (SocketException e) {
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
        
        // Register Chunk Events
        ClientChunkEvents.CHUNK_LOAD.register((world, chunk) -> {
            if (world instanceof net.minecraft.client.multiplayer.ClientLevel) {
                scanExecutor.submit(() -> scanChunk(chunk));
            }
        });
        
        ClientChunkEvents.CHUNK_UNLOAD.register((world, chunk) -> {
            if (world instanceof net.minecraft.client.multiplayer.ClientLevel) {
                // Capture pos before submitting to avoid any reference issues (though chunk.getPos() is safe)
                ChunkPos pos = chunk.getPos();
                scanExecutor.submit(() -> unloadChunk(pos));
            }
        });
    }
    
    // Event Driven Scanning
    public void scanChunk(LevelChunk chunk) {
        if (wantedBlocks.isEmpty()) return;
        
        ChunkPos cPos = chunk.getPos();
        List<BlockPos> foundInChunk = new ArrayList<>();
        List<FoundBlock> added = new ArrayList<>();
        
        // Hardcoded limits as before
        int minY = -64;
        int maxY = 320;
        
        for (int by = minY; by < maxY; by++) {
            for (int bx = 0; bx < 16; bx++) {
                for (int bz = 0; bz < 16; bz++) {
                    BlockPos targetPos = new BlockPos(cPos.getMinBlockX() + bx, by, cPos.getMinBlockZ() + bz);
                    // Note: accessing getBlockState off-thread on a client chunk *can* be risky if the chunk is being modified.
                    // However, for an ESP, occasional tearing is acceptable vs main thread lag.
                    // Ideally we would iterate sections directly, but this is cleaner.
                    BlockState state = chunk.getBlockState(targetPos);
                    if (!state.isAir()) {
                        String id = BuiltInRegistries.BLOCK.getKey(state.getBlock()).getPath();
                        if (wantedBlocks.contains(id)) {
                            foundInChunk.add(targetPos);
                            // If not already known, add to new list
                            if (!knownBlocks.containsKey(targetPos)) {
                                knownBlocks.put(targetPos, id);
                                added.add(new FoundBlock(id, targetPos.getX(), targetPos.getY(), targetPos.getZ()));
                            }
                        }
                    }
                }
            }
        }
        
        chunkCache.put(cPos, foundInChunk);
        
        if (!added.isEmpty()) {
            sendBlockDiff(added, new ArrayList<>());
        }
    }
    
    public void unloadChunk(ChunkPos cPos) {
        List<BlockPos> blocks = chunkCache.remove(cPos);
        if (blocks != null) {
            List<BlockPos> removed = new ArrayList<>();
            for (BlockPos p : blocks) {
                if (knownBlocks.remove(p) != null) {
                    removed.add(p);
                }
            }
            if (!removed.isEmpty()) {
                sendBlockDiff(new ArrayList<>(), removed);
            }
        }
    }
    
    public void handleBlockUpdate(BlockPos pos, BlockState newState) {
        // Offload to scan executor to maintain consistency with scanChunk and avoid main thread work
        if (scanExecutor != null && !scanExecutor.isShutdown()) {
            scanExecutor.submit(() -> {
                if (wantedBlocks.isEmpty()) return;
                
                String id = BuiltInRegistries.BLOCK.getKey(newState.getBlock()).getPath();
                boolean isWanted = wantedBlocks.contains(id);
                boolean wasKnown = knownBlocks.containsKey(pos);
                
                List<FoundBlock> added = new ArrayList<>();
                List<BlockPos> removed = new ArrayList<>();
                
                if (isWanted && !wasKnown) {
                    knownBlocks.put(pos, id);
                    added.add(new FoundBlock(id, pos.getX(), pos.getY(), pos.getZ()));
                    
                    // Add to chunk cache
                    ChunkPos cPos = new ChunkPos(pos);
                    chunkCache.computeIfAbsent(cPos, k -> new ArrayList<>()).add(pos);
                    
                } else if (!isWanted && wasKnown) {
                    knownBlocks.remove(pos);
                    removed.add(pos);
                    
                    // Remove from chunk cache
                    ChunkPos cPos = new ChunkPos(pos);
                    List<BlockPos> list = chunkCache.get(cPos);
                    if (list != null) {
                        list.remove(pos);
                    }
                }
                
                if (!added.isEmpty() || !removed.isEmpty()) {
                    sendBlockDiff(added, removed);
                }
            });
        }
    }

    public void rescanAllLoaded(boolean clear) {
        if (scanExecutor == null || scanExecutor.isShutdown()) return;

         // Clear all - submit to executor to ensure thread safety with other scan ops
        if (clear) {
            scanExecutor.submit(() -> {
                // Send Clear All Packet instead of individual removes
                sendClearAll();
                knownBlocks.clear();
                chunkCache.clear();
            });
        }
        
        if (wantedBlocks.isEmpty()) return;

        Minecraft mc = Minecraft.getInstance();
        if (mc.level != null && mc.player != null) {
            // Collect chunks on main thread to avoid concurrency issues with Level/ChunkSource
            List<LevelChunk> chunksToScan = new ArrayList<>();
            BlockPos playerPos = mc.player.blockPosition();
            int renderDist = mc.options.renderDistance().get();
            // Use a larger radius to capture chunks that are loaded but outside the immediate render distance
            // This fixes the issue where chunks generated/loaded during the scan or at the edge are skipped
            int chunkRadius = Math.max(renderDist + 8, 32);
            
            for (int x = -chunkRadius; x <= chunkRadius; x++) {
                for (int z = -chunkRadius; z <= chunkRadius; z++) {
                     int chunkX = (playerPos.getX() >> 4) + x;
                     int chunkZ = (playerPos.getZ() >> 4) + z;
                     if (mc.level.getChunkSource().hasChunk(chunkX, chunkZ)) {
                         chunksToScan.add(mc.level.getChunk(chunkX, chunkZ));
                     }
                }
            }
            
            // Submit scan tasks
            for (LevelChunk chunk : chunksToScan) {
                scanExecutor.submit(() -> scanChunk(chunk));
            }
        }
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
                } else if (header == 0xB10C0) { // Block List Update
                    int count = in.readInt();
                    Set<String> newWanted = ConcurrentHashMap.newKeySet();
                    for (int i = 0; i < count; i++) {
                        int len = in.readInt();
                        byte[] idBytes = new byte[len];
                        in.readFully(idBytes);
                        String blockId = new String(idBytes);
                        newWanted.add(blockId);
                    }
                    
                    // Calculate Diff
                    List<String> removed = new ArrayList<>();
                    List<String> added = new ArrayList<>();
                    
                    // Use a copy of current wantedBlocks for diff calculation to avoid race conditions
                    Set<String> oldWanted = new java.util.HashSet<>(wantedBlocks);

                    for (String id : oldWanted) {
                        if (!newWanted.contains(id)) removed.add(id);
                    }
                    for (String id : newWanted) {
                        if (!oldWanted.contains(id)) added.add(id);
                    }
                    
                    // Update wantedBlocks atomically-ish to avoid "empty window" race condition
                    // Transition: Old -> Union(Old, New) -> New
                    wantedBlocks.addAll(newWanted);
                    wantedBlocks.retainAll(newWanted);
                    
                    System.out.println("[SocketServer] Block Update. Added: " + added + ", Removed: " + removed);
                    
                    // Handle Removed
                    if (!removed.isEmpty()) {
                        for (String id : removed) {
                            sendDeleteBlockType(id);
                        }
                        // Clean up server cache
                        scanExecutor.submit(() -> {
                            knownBlocks.entrySet().removeIf(entry -> removed.contains(entry.getValue()));
                            // Rebuild chunk cache (expensive but necessary to keep sync)
                            chunkCache.clear();
                            for (BlockPos pos : knownBlocks.keySet()) {
                                ChunkPos cPos = new ChunkPos(pos);
                                chunkCache.computeIfAbsent(cPos, k -> new ArrayList<>()).add(pos);
                            }
                        });
                    }
                    
                    // Handle Added
                    if (!added.isEmpty()) {
                         Minecraft.getInstance().execute(() -> rescanAllLoaded(false));
                    }
                    
                } else if (header == 0xBADF00D) { // Disable Request
                    boolean fully = in.readByte() != 0;
                    System.out.println("Received Disable Request. Fully: " + fully);
                    
                    if (fully) {
                        shutdown(true);
                    } else {
                        shutdown(false);
                    }
                    break;
                }
            }
        } catch (IOException e) {
            // Connection lost
        }
    }
    
    private void sendFullBlockState(DataOutputStream out) {
        if (knownBlocks.isEmpty()) return;
        try {
            synchronized (out) {
                out.writeInt(0x0BE0C4D0); // Header
                out.writeInt(knownBlocks.size()); // Count
                for (Map.Entry<BlockPos, String> entry : knownBlocks.entrySet()) {
                    out.writeByte(0); // ADD
                    BlockPos pos = entry.getKey();
                    out.writeInt(pos.getX());
                    out.writeInt(pos.getY());
                    out.writeInt(pos.getZ());
                    byte[] idBytes = entry.getValue().getBytes(StandardCharsets.UTF_8);
                    out.writeInt(idBytes.length);
                    out.write(idBytes);
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // Old scanner removed

    
    private void sendBlockDiff(List<FoundBlock> added, List<BlockPos> removed) {
        if (clients.isEmpty()) return;
        if (networkExecutor == null || networkExecutor.isShutdown()) return;
        
        // Offload sending to network thread
        networkExecutor.submit(() -> {
            try {
                // Header: 0xBL0C4D0 (199587584)
                for (DataOutputStream out : clients) {
                    synchronized (out) {
                        out.writeInt(0x0BE0C4D0); // Using 0x0BE0C4D0 (Block Data)
                        out.writeInt(added.size() + removed.size());
                        
                        for (BlockPos pos : removed) {
                            out.writeByte(1); // REMOVE
                            out.writeInt(pos.getX());
                            out.writeInt(pos.getY());
                            out.writeInt(pos.getZ());
                        }
                        
                        for (FoundBlock fb : added) {
                            out.writeByte(0); // ADD
                            out.writeInt(fb.x);
                            out.writeInt(fb.y);
                            out.writeInt(fb.z);
                            byte[] idBytes = fb.id.getBytes(StandardCharsets.UTF_8);
                            out.writeInt(idBytes.length);
                            out.write(idBytes);
                        }
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    private void sendDeleteBlockType(String blockId) {
        if (clients.isEmpty()) return;
        if (networkExecutor == null || networkExecutor.isShutdown()) return;

        networkExecutor.submit(() -> {
            try {
                for (DataOutputStream out : clients) {
                    synchronized (out) {
                        out.writeInt(0xB10CDE1); // DELETE BLOCK TYPE
                        byte[] idBytes = blockId.getBytes(StandardCharsets.UTF_8);
                        out.writeInt(idBytes.length);
                        out.write(idBytes);
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    private void sendClearAll() {
        if (clients.isEmpty()) return;
        if (networkExecutor == null || networkExecutor.isShutdown()) return;

        networkExecutor.submit(() -> {
            try {
                for (DataOutputStream out : clients) {
                    synchronized (out) {
                        out.writeInt(0x0C1EA400); // CLEAR ALL
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }
    
    public boolean fullyDisabled = false;

    private void shutdown(boolean fully) {
        running = false;
        fullyDisabled = fully;
        
        // Shutdown Executors
        if (scanExecutor != null) scanExecutor.shutdownNow();
        if (networkExecutor != null) networkExecutor.shutdownNow();
        
        try {
            if (serverSocket != null) {
                serverSocket.close();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        clients.clear();
        moduleStates.clear();
        System.out.println("Overlay Server Stopped. Fully: " + fully);
    }
    
    public void reactivate() {
        if (fullyDisabled) {
            System.out.println("Cannot reactivate: Fully disabled.");
            return;
        }
        if (running) return;
        start();
    }

    private long lastTime = 0;

    private void broadcastData(WorldRenderContext context) {
        if (clients.isEmpty()) return;
        if (networkExecutor == null || networkExecutor.isShutdown()) return;

        long now = System.currentTimeMillis();
        if (now - lastTime < 7) return;
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

            // Collect entities on Main Thread to avoid concurrent modification of the list
            List<Entity> entities = new ArrayList<>();
            for (Entity e : client.level.entitiesForRendering()) {
                entities.add(e);
            }

            // Offload computing and sending to Network Thread
            networkExecutor.submit(() -> {
                try {
                    int entityCount = 0;
                    for (Entity e : entities) {
                        if (e instanceof Player && e != localPlayer) entityCount++;
                    }

                    for (DataOutputStream out : clients) {
                        try {
                            synchronized (out) {
                                out.writeInt(0xCAFEBABE);
                                
                                out.writeFloat(camYaw);
                                out.writeFloat(camPitch);
                                
                                // Send Camera Position for Absolute Coordinate Rendering
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

                                        String name = player.getName().getString();
                                        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
                                        out.writeInt(nameBytes.length);
                                        out.write(nameBytes);

                                        int ping = -1;
                                        if (client.getConnection() != null) {
                                            PlayerInfo info = client.getConnection().getPlayerInfo(player.getUUID());
                                            if (info != null) ping = info.getLatency();
                                        }
                                        out.writeInt(ping);

                                        out.writeFloat(player.getHealth());
                                        out.writeFloat(player.getMaxHealth());
                                        out.writeFloat(player.getAbsorptionAmount());

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
                                                out.writeInt(0);
                                            } else {
                                                String itemId = BuiltInRegistries.ITEM.getKey(stack.getItem()).getPath();
                                                byte[] idBytes = itemId.getBytes(StandardCharsets.UTF_8);
                                                out.writeInt(idBytes.length);
                                                out.write(idBytes);
                                                out.writeInt(stack.getCount());
                                                
                                                if (stack.isDamageableItem()) {
                                                    out.writeInt(stack.getMaxDamage());
                                                    out.writeInt(stack.getDamageValue());
                                                } else {
                                                    out.writeInt(0);
                                                    out.writeInt(0);
                                                }

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
                        } catch (IOException e) {
                            clients.remove(out);
                        }
                    }

                } catch (Exception e) {
                    e.printStackTrace();
                }
            });

        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}