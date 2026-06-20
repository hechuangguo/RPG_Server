/**
 * @file    ClientProtoWire.cpp
 * @brief  ClientProtoWire 实现
 */

#include "ClientProtoWire.h"

rpg::mapdata::EntityType toProtoEntityType(uint8_t legacyType)
{
    switch (legacyType)
    {
    case 0: return rpg::mapdata::ENTITY_TYPE_PLAYER;
    case 1: return rpg::mapdata::ENTITY_TYPE_NPC;
    case 2: return rpg::mapdata::ENTITY_TYPE_MONSTER;
    case 3: return rpg::mapdata::ENTITY_TYPE_PET;
    case 4: return rpg::mapdata::ENTITY_TYPE_ITEM;
    default: return rpg::mapdata::ENTITY_TYPE_UNSPECIFIED;
    }
}

void fillProtoSpawnEntity(uint64_t entityId, const std::string& name, uint32_t level,
                          float x, float y, float z, float dir, uint8_t legacyEntityType,
                          uint32_t modelId, uint32_t animState,
                          rpg::mapdata::S2CSpawnEntity& out)
{
    out.Clear();
    out.set_entity_id(entityId);
    out.set_name(name);
    out.set_level(level);
    out.mutable_pos()->set_x(x);
    out.mutable_pos()->set_y(y);
    out.mutable_pos()->set_z(z);
    out.set_dir(dir);
    out.set_entity_type(toProtoEntityType(legacyEntityType));
    out.set_model_id(modelId);
    out.set_anim_state(animState);
}
