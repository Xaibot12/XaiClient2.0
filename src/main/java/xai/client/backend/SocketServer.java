package xai.client.backend;

import com.mojang.blaze3d.platform.InputConstants;
import net.minecraft.client.Minecraft;

import xai.client.module.BlockESP;

import java.io.DataOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

public class SocketServer {
    private static final SocketServer INSTANCE = new SocketServer();
    private static final int PORT = 25566;

    private ExecutorService networkExecutor;

    private final List<DataOutputStream> clients = new CopyOnWriteArrayList<>();
    private final Map<String, Boolean> moduleStates = new ConcurrentHashMap<>();
    
    // ESP Specific Settings
    private final Set<String> specificMobs = ConcurrentHashMap.newKeySet();
    private volatile boolean showGenericMobs = true;
    private volatile boolean showAllEntities = false;

    // Hotkeys
    private final Set<Integer> watchedHotkeys = ConcurrentHashMap.newKeySet();
    private final Set<Integer> pressedKeys = ConcurrentHashMap.newKeySet();
    
    public Set<String> getSpecificMobs() { return specificMobs; }
    public boolean isShowGenericMobs() { return showGenericMobs; }
    public boolean isShowAllEntities() { return showAllEntities; }
    
    private final List<Runnable> connectionListeners = new CopyOnWriteArrayList<>();
    
    public void addConnectionListener(Runnable listener) {
        connectionListeners.add(listener);
    }
    
    public boolean isModuleEnabled(String name) {
        return moduleStates.getOrDefault(name, true); // Default to true if unknown
    }
    
    private ServerSocket serverSocket;
    private boolean running = false;
    public boolean fullyDisabled = false;

    public void checkHotkeys() {
        Minecraft mc = Minecraft.getInstance();
        if (mc == null || mc.getWindow() == null) return;

        // Check conditions: Focused AND No Screen Open
        if (!mc.isWindowActive() || mc.screen != null) {
            pressedKeys.clear(); // Reset state if context invalid
            return;
        }

        long window = mc.getWindow().getWindow();
        for (Integer key : watchedHotkeys) {
            boolean isDown = InputConstants.isKeyDown(window, key);
            if (isDown) {
                if (!pressedKeys.contains(key)) {
                    // Rising edge: Key just pressed
                    pressedKeys.add(key);
                    sendHotkey(key);
                }
            } else {
                pressedKeys.remove(key);
            }
        }
    }

    private void sendHotkey(int key) {
        if (networkExecutor == null || networkExecutor.isShutdown()) return;
        networkExecutor.submit(() -> {
            for (DataOutputStream out : clients) {
                try {
                    synchronized (out) {
                        out.writeInt(0xCB14D);
                        out.writeInt(key);
                    }
                } catch (IOException e) {
                    // Ignore
                }
            }
        });
    }
    
    public static SocketServer getInstance() {
        return INSTANCE;
    }

    public void start() {
        if (running || fullyDisabled) return;
        running = true;
        
        networkExecutor = Executors.newCachedThreadPool(r -> {
            Thread t = new Thread(r, "Xai-Network-Thread");
            t.setDaemon(true);
            return t;
        });
        
        networkExecutor.submit(() -> {
            try {
                serverSocket = new ServerSocket(PORT);
                System.out.println("Overlay Server started on port " + PORT);
                
                while (running && !serverSocket.isClosed()) {
                    try {
                        Socket socket = serverSocket.accept();
                        socket.setTcpNoDelay(true);
                        
                        DataOutputStream out = new DataOutputStream(socket.getOutputStream());
                        clients.add(out);
                        
                        // Notify listeners (e.g. ESP to clear cache)
                        for (Runnable r : connectionListeners) {
                            r.run();
                        }
                        
                        // Send current state
                        BlockESP.getInstance().sendFullState(out);
                        
                        networkExecutor.submit(() -> handleClientRead(socket));
                        
                    } catch (IOException e) {
                        if (running) e.printStackTrace();
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    public boolean hasClients() {
        return !clients.isEmpty();
    }

    // Generic Send Method
    public void sendData(Consumer<DataOutputStream> writer) {
        sendData(writer, null);
    }

    public void sendData(Consumer<DataOutputStream> writer, Runnable onComplete) {
        if (clients.isEmpty()) {
            if (onComplete != null) onComplete.run();
            return;
        }
        if (networkExecutor == null || networkExecutor.isShutdown()) {
            if (onComplete != null) onComplete.run();
            return;
        }
        
        networkExecutor.submit(() -> {
            for (DataOutputStream out : clients) {
                synchronized (out) {
                    try {
                        writer.accept(out);
                        out.flush();
                    } catch (Exception e) {
                        clients.remove(out);
                        e.printStackTrace();
                    }
                }
            }
            if (onComplete != null) onComplete.run();
        });
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
                    
                    BlockESP.getInstance().updateWantedBlocks(newWanted);
                    
                } else if (header == 0xE581) { // ESP Settings Update
                    boolean generic = in.readByte() != 0;
                    boolean all = in.readByte() != 0;
                    int count = in.readInt();
                    specificMobs.clear();
                    for (int i = 0; i < count; i++) {
                        int len = in.readInt();
                        byte[] bytes = new byte[len];
                        in.readFully(bytes);
                        specificMobs.add(new String(bytes, java.nio.charset.StandardCharsets.UTF_8));
                    }
                    showGenericMobs = generic;
                    showAllEntities = all;
                } else if (header == 0xB14D0) { // Set Hotkeys
                    int count = in.readInt();
                    watchedHotkeys.clear();
                    for (int i = 0; i < count; i++) {
                        watchedHotkeys.add(in.readInt());
                    }
                } else if (header == 0xBADF00D) { // Disable Request
                    shutdown(true);
                }
            }
        } catch (IOException e) {
            clients.removeIf(out -> {
                try {
                    // This is a bit hacky to find the matching output stream, 
                    // but for now we rely on the write failure to remove dead clients.
                    return false; 
                } catch (Exception ex) { return true; }
            });
        }
    }
    
    public void reactivate() {
        if (fullyDisabled) return;
        if (running) return;
        start();
    }
    
    private void shutdown(boolean fully) {
        running = false;
        fullyDisabled = fully;
        
        BlockESP.getInstance().stop();
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
}
