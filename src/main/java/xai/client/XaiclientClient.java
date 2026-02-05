package xai.client;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import xai.client.backend.SocketServer;

public class XaiclientClient implements ClientModInitializer {
    @Override
    public void onInitializeClient() {
        SocketServer.getInstance().start();
    }
}
