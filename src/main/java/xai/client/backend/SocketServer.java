package xai.client.backend;

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
    
    private ServerSocket serverSocket;
    private boolean running = false;
    public boolean fullyDisabled = false;

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

    // Generic Send Method
    public void sendData(Consumer<DataOutputStream> writer) {
        if (clients.isEmpty()) return;
        if (networkExecutor == null || networkExecutor.isShutdown()) return;
        
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
