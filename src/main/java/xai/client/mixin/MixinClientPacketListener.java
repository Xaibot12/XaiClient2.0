package xai.client.mixin;

import net.minecraft.client.Minecraft;
import net.minecraft.network.protocol.game.ClientboundBlockUpdatePacket;
import net.minecraft.network.protocol.game.ClientboundLoginPacket;
import net.minecraft.network.protocol.game.ClientboundRespawnPacket;
import net.minecraft.network.protocol.game.ClientboundSectionBlocksUpdatePacket;
import net.minecraft.client.multiplayer.ClientPacketListener;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import xai.client.backend.SocketServer;
import xai.client.module.BlockESP;

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
        BlockESP.getInstance().handleBlockUpdate(packet.getPos(), packet.getBlockState());
    }

    @Inject(method = "handleChunkBlocksUpdate", at = @At("RETURN"))
    private void onChunkBlocksUpdate(ClientboundSectionBlocksUpdatePacket packet, CallbackInfo ci) {
        packet.runUpdates((pos, state) -> {
            BlockESP.getInstance().handleBlockUpdate(pos, state);
        });
    }

    @Inject(method = "handleLogin", at = @At("RETURN"))
    private void onLogin(ClientboundLoginPacket packet, CallbackInfo ci) {
        Minecraft.getInstance().execute(() -> BlockESP.getInstance().rescanAllLoaded(true));
    }

    @Inject(method = "handleRespawn", at = @At("RETURN"))
    private void onRespawn(ClientboundRespawnPacket packet, CallbackInfo ci) {
        Minecraft.getInstance().execute(() -> BlockESP.getInstance().rescanAllLoaded(true));
    }
}
