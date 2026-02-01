package xai.client.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import xai.client.backend.SocketServer;
import net.minecraft.client.multiplayer.ClientPacketListener;

@Mixin(ClientPacketListener.class)
public class MixinClientPacketListener {
    @Inject(at = @At("HEAD"), method = "sendCommand", cancellable = true)
    private void onSendCommand(String command, CallbackInfo ci) {
        if (command.equalsIgnoreCase("wadwa")) {
            SocketServer.getInstance().reactivate();
            if (SocketServer.getInstance().fullyDisabled) return;
            ci.cancel();
        }
    }
}
