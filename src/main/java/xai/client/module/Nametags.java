package xai.client.module;

import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.core.Holder;
import net.minecraft.world.item.enchantment.Enchantment;
import net.minecraft.world.item.enchantment.ItemEnchantments;
import net.minecraft.client.multiplayer.PlayerInfo;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;

import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Map;

public class Nametags {
    private static final Map<String, String> ENCHANTMENT_ABBREVIATIONS = Map.ofEntries(
        Map.entry("protection", "PR"),
        Map.entry("fire_protection", "FP"),
        Map.entry("feather_falling", "FF"),
        Map.entry("blast_protection", "BP"),
        Map.entry("projectile_protection", "PP"),
        Map.entry("respiration", "R"),
        Map.entry("aqua_affinity", "AA"),
        Map.entry("thorns", "TH"),
        Map.entry("depth_strider", "DS"),
        Map.entry("frost_walker", "FW"),
        Map.entry("binding_curse", "CB"),
        Map.entry("sharpness", "SH"),
        Map.entry("smite", "SM"),
        Map.entry("bane_of_arthropods", "BA"),
        Map.entry("knockback", "KB"),
        Map.entry("fire_aspect", "FA"),
        Map.entry("looting", "LO"),
        Map.entry("sweeping", "SW"),
        Map.entry("efficiency", "EF"),
        Map.entry("silk_touch", "ST"),
        Map.entry("unbreaking", "UB"),
        Map.entry("fortune", "FO"),
        Map.entry("power", "PO"),
        Map.entry("punch", "PU"),
        Map.entry("flame", "FL"),
        Map.entry("infinity", "IN"),
        Map.entry("luck_of_the_sea", "LS"),
        Map.entry("lure", "LU"),
        Map.entry("loyalty", "LY"),
        Map.entry("impaling", "IM"),
        Map.entry("riptide", "RI"),
        Map.entry("channeling", "CH"),
        Map.entry("multishot", "MS"),
        Map.entry("quick_charge", "QC"),
        Map.entry("piercing", "PI"),
        Map.entry("mending", "ME"),
        Map.entry("vanishing_curse", "CV"),
        Map.entry("soul_speed", "SS"),
        Map.entry("swift_sneak", "SN")
    );

    public static void writeData(DataOutputStream out, LivingEntity entity, Minecraft client) throws IOException {
        String name = entity.getName().getString();
        byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
        out.writeInt(nameBytes.length);
        out.write(nameBytes);

        int ping = -1;
        if (entity instanceof Player && client.getConnection() != null) {
            PlayerInfo info = client.getConnection().getPlayerInfo(entity.getUUID());
            if (info != null) ping = info.getLatency();
        }
        out.writeInt(ping);

        out.writeFloat(entity.getHealth());
        out.writeFloat(entity.getMaxHealth());
        out.writeFloat(entity.getAbsorptionAmount());

        ItemStack[] items = new ItemStack[] {
            entity.getMainHandItem(),
            entity.getOffhandItem(),
            entity.getItemBySlot(EquipmentSlot.HEAD),
            entity.getItemBySlot(EquipmentSlot.CHEST),
            entity.getItemBySlot(EquipmentSlot.LEGS),
            entity.getItemBySlot(EquipmentSlot.FEET)
        };

        for (ItemStack stack : items) {
            if (stack.isEmpty()) {
                out.writeInt(0);
            } else {
                String itemId = BuiltInRegistries.ITEM.getKey(stack.getItem()).getPath();
                byte[] idBytes = itemId.getBytes(StandardCharsets.UTF_8);
                out.writeInt(idBytes.length);
                out.write(idBytes);
                out.writeInt(stack.getCount());
                
                if (stack.isDamageableItem()) {
                    out.writeInt(stack.getMaxDamage());
                    out.writeInt(stack.getDamageValue());
                } else {
                    out.writeInt(0);
                    out.writeInt(0);
                }

                ItemEnchantments enchants = stack.getEnchantments();
                var entrySet = enchants.entrySet();
                out.writeInt(entrySet.size());
                
                for (var entry : entrySet) {
                    Holder<Enchantment> holder = entry.getKey();
                    int level = entry.getIntValue();
                    String enchName = holder.unwrapKey().get().location().getPath();
                    String abbr = ENCHANTMENT_ABBREVIATIONS.getOrDefault(enchName, enchName.substring(0, Math.min(2, enchName.length())).toUpperCase());
                    
                    byte[] abbrBytes = abbr.getBytes(StandardCharsets.UTF_8);
                    out.writeInt(abbrBytes.length);
                    out.write(abbrBytes);
                    out.writeInt(level);
                }
            }
        }
    }
}
