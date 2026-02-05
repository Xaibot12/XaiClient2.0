package xai.client.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import net.minecraft.network.protocol.game.ClientboundBlockUpdatePacket;
import net.minecraft.network.protocol.game.ClientboundSectionBlocksUpdatePacket;
import net.minecraft.network.protocol.game.ClientboundLoginPacket;
import net.minecraft.network.protocol.game.ClientboundRespawnPacket;
import net.minecraft.client.Minecraft;
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

    @Inject(method = "handleBlockUpdate", at = @At("RETURN"))
    private void onBlockUpdate(ClientboundBlockUpdatePacket packet, CallbackInfo ci) {
        SocketServer.getInstance().handleBlockUpdate(packet.getPos(), packet.getBlockState());
    }

    @Inject(method = "handleChunkBlocksUpdate", at = @At("RETURN"))
    private void onChunkBlocksUpdate(ClientboundSectionBlocksUpdatePacket packet, CallbackInfo ci) {
        packet.runUpdates((pos, state) -> {
            SocketServer.getInstance().handleBlockUpdate(pos, state);
        });
    }

    @Inject(method = "handleLogin", at = @At("RETURN"))
    private void onLogin(ClientboundLoginPacket packet, CallbackInfo ci) {
        Minecraft.getInstance().execute(() -> SocketServer.getInstance().rescanAllLoaded(true));
    }

    @Inject(method = "handleRespawn", at = @At("RETURN"))
    private void onRespawn(ClientboundRespawnPacket packet, CallbackInfo ci) {
        Minecraft.getInstance().execute(() -> SocketServer.getInstance().rescanAllLoaded(true));
    }
}
