package xai.client;

import net.fabricmc.api.ClientModInitializer;
import xai.client.backend.SocketServer;

public class XaiclientClient implements ClientModInitializer {
    @Override
    public void onInitializeClient() {
        SocketServer.getInstance().start();
    }
}
