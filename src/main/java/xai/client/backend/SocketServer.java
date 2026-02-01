package xai.client.backend;

import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.WorldRenderEvents;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;

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

public class SocketServer {
    private static final SocketServer INSTANCE = new SocketServer();
    private static final int PORT = 25566;

    private final List<DataOutputStream> clients = new CopyOnWriteArrayList<>();
    private final Map<String, Boolean> moduleStates = new ConcurrentHashMap<>();
    private ServerSocket serverSocket;
    private boolean running = false;

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
                
                // Entity Count
                out.writeInt(entityCount);

                // Entity List
                for (Entity entity : allEntities) {
                    if (entity instanceof Player && entity != client.player) {
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
