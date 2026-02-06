package xai.client.module;

import xai.client.backend.SocketServer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.client.Minecraft;
import net.minecraft.world.level.Level;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientChunkEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;

import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class BlockESP {
    private static final BlockESP INSTANCE = new BlockESP();
    
    // State
    private final Set<String> wantedBlocks = ConcurrentHashMap.newKeySet();
    private final Map<BlockPos, String> knownBlocks = new ConcurrentHashMap<>();
    private final Map<ChunkPos, List<BlockPos>> chunkCache = new ConcurrentHashMap<>();
    private final Set<ChunkPos> sentChunks = ConcurrentHashMap.newKeySet();
    
    // Executor for scanning (keep separate from Network)
    private ExecutorService scanExecutor;
    
    public static class FoundBlock {
        public String id;
        public int x, y, z;
        public FoundBlock(String id, int x, int y, int z) {
            this.id = id; this.x = x; this.y = y; this.z = z;
        }
    }
    
    public static BlockESP getInstance() {
        return INSTANCE;
    }
    
    public void start() {
        if (scanExecutor == null || scanExecutor.isShutdown()) {
            scanExecutor = Executors.newSingleThreadExecutor(r -> new Thread(r, "Xai-Scan-Thread"));
        }

        // Register Events
        ClientChunkEvents.CHUNK_LOAD.register((world, chunk) -> {
            if (world instanceof net.minecraft.client.multiplayer.ClientLevel) {
                scanChunk(chunk);
            }
        });
        
        ClientChunkEvents.CHUNK_UNLOAD.register((world, chunk) -> {
            if (world instanceof net.minecraft.client.multiplayer.ClientLevel) {
                unloadChunk(chunk.getPos());
            }
        });
        
        // Periodic check for missing chunks
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (client.level != null && client.player != null && client.player.tickCount % 100 == 0) { // Every 5 seconds
                ensureChunksSent();
            }
        });
    }
    
    public void stop() {
        if (scanExecutor != null) {
            scanExecutor.shutdownNow();
        }
        knownBlocks.clear();
        chunkCache.clear();
        sentChunks.clear();
    }
    
    public void updateWantedBlocks(Set<String> newWanted) {
        Set<String> oldWanted = new HashSet<>(wantedBlocks);
        List<String> removed = new ArrayList<>();
        List<String> added = new ArrayList<>();
        
        for (String id : oldWanted) {
            if (!newWanted.contains(id)) removed.add(id);
        }
        for (String id : newWanted) {
            if (!oldWanted.contains(id)) added.add(id);
        }
        
        wantedBlocks.addAll(newWanted);
        wantedBlocks.retainAll(newWanted);
        
        System.out.println("[BlockESP] Update. Added: " + added + ", Removed: " + removed);
        
        if (!removed.isEmpty()) {
            for (String id : removed) {
                sendDeleteBlockType(id);
            }
            scanExecutor.submit(() -> {
                knownBlocks.entrySet().removeIf(entry -> removed.contains(entry.getValue()));
                // Rebuild chunk cache
                chunkCache.clear();
                for (BlockPos pos : knownBlocks.keySet()) {
                    ChunkPos cPos = new ChunkPos(pos);
                    chunkCache.computeIfAbsent(cPos, k -> new ArrayList<>()).add(pos);
                }
            });
        }
        
        if (!added.isEmpty()) {
             // Only scan for the NEW blocks
             scanExecutor.submit(() -> scanNewBlocks(added));
        }
    }
    
    // Scan specific blocks in all currently loaded chunks
    private void scanNewBlocks(List<String> newBlockIds) {
        if (newBlockIds.isEmpty()) return;
        
        Minecraft mc = Minecraft.getInstance();
        if (mc.level == null) return;
        
        List<LevelChunk> chunksToScan = new ArrayList<>();
        mc.executeBlocking(() -> {
            if (mc.level != null) {
                int r = mc.options.renderDistance().get();
                BlockPos p = mc.player.blockPosition();
                int cx = p.getX() >> 4;
                int cz = p.getZ() >> 4;
                
                for (int x = -r - 2; x <= r + 2; x++) {
                    for (int z = -r - 2; z <= r + 2; z++) {
                         if (mc.level.getChunkSource().hasChunk(cx + x, cz + z)) {
                             chunksToScan.add(mc.level.getChunk(cx + x, cz + z));
                         }
                    }
                }
                
                // Sort chunks by distance from player (Center Outwards)
                chunksToScan.sort(Comparator.comparingDouble(c -> {
                    if (c == null) return Double.MAX_VALUE;
                    int dx = c.getPos().x - cx;
                    int dz = c.getPos().z - cz;
                    return dx * dx + dz * dz;
                }));
            }
        });
        
        for (LevelChunk chunk : chunksToScan) {
            if (chunk == null) continue;
            try {
                scanChunkForSpecificBlocks(chunk, newBlockIds);
            } catch (Exception e) {
                System.err.println("[BlockESP] Error scanning chunk " + chunk.getPos() + ": " + e.getMessage());
            }
        }
    }
    
    private void scanChunkForSpecificBlocks(LevelChunk chunk, List<String> blockIds) {
        ChunkPos cPos = chunk.getPos();
        List<FoundBlock> added = new ArrayList<>();
        
        int minY = -64;
        int maxY = 320;
        
        for (int by = minY; by < maxY; by++) {
            for (int bx = 0; bx < 16; bx++) {
                for (int bz = 0; bz < 16; bz++) {
                    BlockPos targetPos = new BlockPos(cPos.getMinBlockX() + bx, by, cPos.getMinBlockZ() + bz);
                    BlockState state = chunk.getBlockState(targetPos);
                    if (!state.isAir()) {
                        String id = BuiltInRegistries.BLOCK.getKey(state.getBlock()).getPath();
                        if (blockIds.contains(id)) {
                             if (!knownBlocks.containsKey(targetPos)) {
                                knownBlocks.put(targetPos, id);
                                chunkCache.computeIfAbsent(cPos, k -> new ArrayList<>()).add(targetPos);
                                added.add(new FoundBlock(id, targetPos.getX(), targetPos.getY(), targetPos.getZ()));
                            }
                        }
                    }
                }
            }
        }
        
        if (!added.isEmpty()) {
            sendBlockDiff(added, new ArrayList<>());
        }
    }

    public void scanChunk(LevelChunk chunk) {
        ChunkPos cPos = chunk.getPos();
        // Mark as sent/scanned
        sentChunks.add(cPos);
        
        if (wantedBlocks.isEmpty()) return;
        
        List<BlockPos> foundInChunk = new ArrayList<>();
        List<FoundBlock> added = new ArrayList<>();
        
        int minY = -64;
        int maxY = 320;
        
        for (int by = minY; by < maxY; by++) {
            for (int bx = 0; bx < 16; bx++) {
                for (int bz = 0; bz < 16; bz++) {
                    BlockPos targetPos = new BlockPos(cPos.getMinBlockX() + bx, by, cPos.getMinBlockZ() + bz);
                    BlockState state = chunk.getBlockState(targetPos);
                    if (!state.isAir()) {
                        String id = BuiltInRegistries.BLOCK.getKey(state.getBlock()).getPath();
                        if (wantedBlocks.contains(id)) {
                            foundInChunk.add(targetPos);
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
        sentChunks.remove(cPos);
        List<BlockPos> blocks = chunkCache.remove(cPos);
        if (blocks != null) {
            for (BlockPos p : blocks) {
                knownBlocks.remove(p);
            }
        }
        // Send Chunk Unload Packet
        sendChunkUnload(cPos.x, cPos.z);
    }
    
    public void handleBlockUpdate(BlockPos pos, BlockState newState) {
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
                    ChunkPos cPos = new ChunkPos(pos);
                    chunkCache.computeIfAbsent(cPos, k -> new ArrayList<>()).add(pos);
                } else if (!isWanted && wasKnown) {
                    knownBlocks.remove(pos);
                    removed.add(pos);
                    ChunkPos cPos = new ChunkPos(pos);
                    List<BlockPos> list = chunkCache.get(cPos);
                    if (list != null) list.remove(pos);
                }
                
                if (!added.isEmpty() || !removed.isEmpty()) {
                    sendBlockDiff(added, removed);
                }
            });
        }
    }
    
    public void rescanAllLoaded(boolean clear) {
        if (clear) clear();
        Minecraft mc = Minecraft.getInstance();
        if (mc.level == null) return;
        
        mc.execute(() -> {
            if (mc.level == null) return;
            int r = mc.options.renderDistance().get();
            BlockPos p = mc.player.blockPosition();
            int cx = p.getX() >> 4;
            int cz = p.getZ() >> 4;
            
            List<LevelChunk> chunksToScan = new ArrayList<>();
            for (int x = -r - 2; x <= r + 2; x++) {
                for (int z = -r - 2; z <= r + 2; z++) {
                     if (mc.level.getChunkSource().hasChunk(cx + x, cz + z)) {
                         chunksToScan.add(mc.level.getChunk(cx + x, cz + z));
                     }
                }
            }
            
            // Sort chunks by distance from player (Center Outwards)
            chunksToScan.sort(Comparator.comparingDouble(c -> {
                int dx = c.getPos().x - cx;
                int dz = c.getPos().z - cz;
                return dx * dx + dz * dz;
            }));

            if (scanExecutor != null) {
                for (LevelChunk chunk : chunksToScan) {
                    scanExecutor.submit(() -> scanChunk(chunk));
                }
            }
        });
    }

    public void ensureChunksSent() {
        if (scanExecutor == null || scanExecutor.isShutdown()) return;
        
        Minecraft mc = Minecraft.getInstance();
        if (mc.level == null) return;
        
        mc.execute(() -> {
             if (mc.level == null) return;
             int r = mc.options.renderDistance().get();
             BlockPos p = mc.player.blockPosition();
             int cx = p.getX() >> 4;
             int cz = p.getZ() >> 4;
             
             List<LevelChunk> missingChunks = new ArrayList<>();
             
             for (int x = -r; x <= r; x++) {
                for (int z = -r; z <= r; z++) {
                     int targetX = cx + x;
                     int targetZ = cz + z;
                     ChunkPos cp = new ChunkPos(targetX, targetZ);
                     
                     if (mc.level.getChunkSource().hasChunk(targetX, targetZ)) {
                         if (!sentChunks.contains(cp)) {
                             missingChunks.add(mc.level.getChunk(targetX, targetZ));
                         }
                     }
                }
             }
             
             if (!missingChunks.isEmpty()) {
                 // Sort chunks by distance from player (Center Outwards)
                 missingChunks.sort(Comparator.comparingDouble(c -> {
                     int dx = c.getPos().x - cx;
                     int dz = c.getPos().z - cz;
                     return dx * dx + dz * dz;
                 }));

                 scanExecutor.submit(() -> {
                     for (LevelChunk c : missingChunks) {
                         scanChunk(c);
                     }
                 });
             }
        });
    }

    private void sendBlockDiff(List<FoundBlock> added, List<BlockPos> removed) {
        SocketServer.getInstance().sendData(out -> {
            try {
                out.writeInt(0x0BE0C4D0); // Header
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
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    private void sendDeleteBlockType(String blockId) {
        SocketServer.getInstance().sendData(out -> {
            try {
                out.writeInt(0xB10CDE1);
                byte[] idBytes = blockId.getBytes(StandardCharsets.UTF_8);
                out.writeInt(idBytes.length);
                out.write(idBytes);
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }
    
    private void sendChunkUnload(int cx, int cz) {
        SocketServer.getInstance().sendData(out -> {
            try {
                out.writeInt(0xC400000); // CHUNK UNLOAD Header
                out.writeInt(cx);
                out.writeInt(cz);
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    public void sendFullState(DataOutputStream out) {
        if (knownBlocks.isEmpty()) return;
        try {
            out.writeInt(0x0BE0C4D0);
            out.writeInt(knownBlocks.size());
            for (Map.Entry<BlockPos, String> entry : knownBlocks.entrySet()) {
                out.writeByte(0);
                BlockPos pos = entry.getKey();
                out.writeInt(pos.getX());
                out.writeInt(pos.getY());
                out.writeInt(pos.getZ());
                byte[] idBytes = entry.getValue().getBytes(StandardCharsets.UTF_8);
                out.writeInt(idBytes.length);
                out.write(idBytes);
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
    
    public void clear() {
        knownBlocks.clear();
        chunkCache.clear();
        sentChunks.clear();
    }
}
