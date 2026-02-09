package xai.client;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import xai.client.backend.SocketServer;
import xai.client.module.BlockESP;
import xai.client.module.ESP;

public class XaiclientClient implements ClientModInitializer {
    @Override
    public void onInitializeClient() {
        SocketServer.getInstance().start();
        BlockESP.getInstance().start();
        ESP.getInstance().start();
        
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            SocketServer.getInstance().checkHotkeys();
        });
    }
}
