/*
 * Copyright (c) 2010-2022 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "thingtypemanager.h"
#include "creature.h"
#include "creatures.h"
#include "game.h"
#include "itemtype.h"
#include "thingtype.h"

#include <framework/core/binarytree.h>
#include <framework/core/filestream.h>
#include <framework/core/resourcemanager.h>
#include <framework/otml/otml.h>
#include <framework/xml/tinyxml.h>

#include <client/spriteappearances.h>
#include <client/spritemanager.h>

#include "protobuf/appearances.pb.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace otclient::protobuf;

ThingTypeManager g_things;

void ThingTypeManager::init()
{
    m_nullThingType = ThingTypePtr(new ThingType);
    m_nullItemType = ItemTypePtr(new ItemType);
    m_datSignature = 0;
    m_contentRevision = 0;
    m_otbMinorVersion = 0;
    m_otbMajorVersion = 0;
    m_datLoaded = false;
    m_xmlLoaded = false;
    m_otbLoaded = false;
    for (auto& m_thingType : m_thingTypes)
        m_thingType.resize(1, m_nullThingType);
    m_itemTypes.resize(1, m_nullItemType);
}

void ThingTypeManager::terminate()
{
    for (auto& m_thingType : m_thingTypes)
        m_thingType.clear();
    m_itemTypes.clear();
    m_reverseItemTypes.clear();
    m_nullThingType = nullptr;
    m_nullItemType = nullptr;
}

void ThingTypeManager::saveDat(const std::string& fileName)
{
    if (!m_datLoaded)
        stdext::throw_exception("failed to save, dat is not loaded");

    try {
        const FileStreamPtr fin = g_resources.createFile(fileName);
        if (!fin)
            stdext::throw_exception(stdext::format("failed to open file '%s' for write", fileName));

        fin->cache();

        fin->addU32(m_datSignature);

        for (auto& m_thingType : m_thingTypes)
            fin->addU16(m_thingType.size() - 1);

        for (int category = 0; category < ThingLastCategory; ++category) {
            uint16_t firstId = 1;
            if (category == ThingCategoryItem)
                firstId = 100;

            for (uint16_t id = firstId; id < m_thingTypes[category].size(); ++id)
                m_thingTypes[category][id]->serialize(fin);
        }

        fin->flush();
        fin->close();
    } catch (std::exception& e) {
        g_logger.error(stdext::format("Failed to save '%s': %s", fileName, e.what()));
    }
}

bool ThingTypeManager::loadDat(std::string file)
{
    m_datLoaded = false;
    m_datSignature = 0;
    m_contentRevision = 0;
    try {
        file = g_resources.guessFilePath(file, "dat");

        const FileStreamPtr fin = g_resources.openFile(file);
        fin->cache();

    #if ENABLE_ENCRYPTION == 1
        ResourceManager::decrypt(fin->m_data.data(), fin->m_data.size());
    #endif

        m_datSignature = fin->getU32();
        m_contentRevision = static_cast<uint16_t>(m_datSignature);

        for (auto& thingType : m_thingTypes) {
            const int count = fin->getU16() + 1;
            thingType.clear();
            thingType.resize(count, m_nullThingType);
        }

        for (int category = -1; ++category < ThingLastCategory;) {
            const uint16_t firstId = category == ThingCategoryItem ? 100 : 1;

            for (uint16_t id = firstId - 1, s = m_thingTypes[category].size(); ++id < s;) {
                const ThingTypePtr type(new ThingType);
                type->unserialize(id, static_cast<ThingCategory>(category), fin);
                m_thingTypes[category][id] = type;
            }
        }

        m_datLoaded = true;
        g_lua.callGlobalField("g_things", "onLoadDat", file);
        return true;
    } catch (stdext::exception& e) {
        g_logger.error(stdext::format("Failed to read dat '%s': %s'", file, e.what()));
        return false;
    }
}

bool ThingTypeManager::loadOtml(std::string file)
{
    try {
        file = g_resources.guessFilePath(file, "otml");

        const OTMLDocumentPtr doc = OTMLDocument::parse(file);
        for (const OTMLNodePtr& node : doc->children()) {
            ThingCategory category;
            if (node->tag() == "creatures")
                category = ThingCategoryCreature;
            else if (node->tag() == "items")
                category = ThingCategoryItem;
            else if (node->tag() == "effects")
                category = ThingCategoryEffect;
            else if (node->tag() == "missiles")
                category = ThingCategoryMissile;
            else {
                throw OTMLException(node, "not a valid thing category");
            }

            for (const OTMLNodePtr& node2 : node->children()) {
                const auto id = stdext::safe_cast<uint16_t>(node2->tag());
                const auto& type = getThingType(id, category);
                if (!type)
                    throw OTMLException(node2, "thing not found");
                type->unserializeOtml(node2);
            }
        }
        return true;
    } catch (std::exception& e) {
        g_logger.error(stdext::format("Failed to read dat otml '%s': %s'", file, e.what()));
        return false;
    }
}

void ThingTypeManager::loadOtb(const std::string& file)
{
    try {
        const FileStreamPtr fin = g_resources.openFile(file);
        fin->cache();

        uint32_t signature = fin->getU32();
        if (signature != 0)
            stdext::throw_exception("invalid otb file");

        const BinaryTreePtr root = fin->getBinaryTree();
        root->skip(1); // otb first byte is always 0

        signature = root->getU32();
        if (signature != 0)
            stdext::throw_exception("invalid otb file");

        const uint8_t rootAttr = root->getU8();
        if (rootAttr == 0x01) { // OTB_ROOT_ATTR_VERSION
            const uint16_t size = root->getU16();
            if (size != 4 + 4 + 4 + 128)
                stdext::throw_exception("invalid otb root attr version size");

            m_otbMajorVersion = root->getU32();
            m_otbMinorVersion = root->getU32();
            root->skip(4); // buildNumber
            root->skip(128); // description
        }

        const BinaryTreeVec children = root->getChildren();
        m_reverseItemTypes.clear();
        m_itemTypes.resize(children.size() + 1, m_nullItemType);
        m_reverseItemTypes.resize(children.size() + 1, m_nullItemType);

        for (const BinaryTreePtr& node : children) {
            ItemTypePtr itemType(new ItemType);
            itemType->unserialize(node);
            addItemType(itemType);

            const uint16_t clientId = itemType->getClientId();
            if (unlikely(clientId >= m_reverseItemTypes.size()))
                m_reverseItemTypes.resize(clientId + 1);
            m_reverseItemTypes[clientId] = itemType;
        }

        m_otbLoaded = true;
        g_lua.callGlobalField("g_things", "onLoadOtb", file);
    } catch (std::exception& e) {
        g_logger.error(stdext::format("Failed to load '%s' (OTB file): %s", file, e.what()));
    }
}

void ThingTypeManager::loadXml(const std::string& file)
{
    try {
        if (!isOtbLoaded())
            stdext::throw_exception("OTB must be loaded before XML");

        TiXmlDocument doc;
        doc.Parse(g_resources.readFileContents(file).c_str());
        if (doc.Error())
            stdext::throw_exception(stdext::format("failed to parse '%s': '%s'", file, doc.ErrorDesc()));

        TiXmlElement* root = doc.FirstChildElement();
        if (!root || root->ValueTStr() != "items")
            stdext::throw_exception("invalid root tag name");

        for (TiXmlElement* element = root->FirstChildElement(); element; element = element->NextSiblingElement()) {
            if (unlikely(element->ValueTStr() != "item"))
                continue;

            const auto id = element->readType<uint16_t>("id");
            if (id != 0) {
                std::vector<std::string> s_ids = stdext::split(element->Attribute("id"), ";");
                for (const std::string& s : s_ids) {
                    std::vector<int32_t> ids = stdext::split<int32_t>(s, "-");
                    if (ids.size() > 1) {
                        int32_t i = ids[0];
                        while (i <= ids[1])
                            parseItemType(++i, element);
                    } else
                        parseItemType(atoi(s.c_str()), element);
                }
            } else {
                std::vector<int32_t> begin = stdext::split<int32_t>(element->Attribute("fromid"), ";");
                std::vector<int32_t> end = stdext::split<int32_t>(element->Attribute("toid"), ";");
                if (begin[0] && begin.size() == end.size()) {
                    const size_t size = begin.size();
                    for (size_t i = 0; i < size; ++i)
                        while (begin[i] <= end[i])
                            parseItemType(++begin[i], element);
                }
            }
        }

        doc.Clear();
        m_xmlLoaded = true;
        g_logger.debug("items.xml read successfully.");
    } catch (std::exception& e) {
        g_logger.error(stdext::format("Failed to load '%s' (XML file): %s", file, e.what()));
    }
}

bool ThingTypeManager::loadAppearances(const std::string& file)
{
    try {
        int spritesCount = 0;
        std::string appearancesFile;

        json document = json::parse(g_resources.readFileContents(g_resources.resolvePath(g_resources.guessFilePath(file, "json"))));
        for (const auto& obj : document) {
            const auto& type = obj["type"];
            if (type == "appearances") {
                appearancesFile = obj["file"];
            } else if (type == "sprite") {
                int lastSpriteId = obj["lastspriteid"].get<int>();
                g_spriteAppearances.addSpriteSheet(SpriteSheetPtr(new SpriteSheet(obj["firstspriteid"].get<int>(), lastSpriteId, static_cast<SpriteLayout>(obj["spritetype"].get<int>()), obj["file"].get<std::string>())));
                spritesCount = std::max<int>(spritesCount, lastSpriteId);
            }
        }

        g_spriteAppearances.setSpritesCount(spritesCount + 1);

        // load appearances.dat
        std::stringstream fin;
        g_resources.readFileStream(g_resources.resolvePath(stdext::format("/things/%d/%s", g_game.getClientVersion(), appearancesFile)), fin);

        auto appearancesLib = appearances::Appearances();
        if (!appearancesLib.ParseFromIstream(&fin)) {
            throw stdext::exception("Couldn't parse appearances lib.");
        }

        for (int category = ThingCategoryItem; category < ThingLastCategory; ++category) {
            const google::protobuf::RepeatedPtrField<appearances::Appearance>* appearances = nullptr;

            switch (category) {
                case ThingCategoryItem: appearances = &appearancesLib.object(); break;
                case ThingCategoryCreature: appearances = &appearancesLib.outfit(); break;
                case ThingCategoryEffect: appearances = &appearancesLib.effect(); break;
                case ThingCategoryMissile: appearances = &appearancesLib.missile(); break;
                default: return false;
            }

            const auto& lastAppearance = appearances->Get(appearances->size() - 1);

            auto& things = m_thingTypes[category];
            things.clear();
            things.resize(lastAppearance.id() + 1, m_nullThingType);

            for (const auto& appearance : *appearances) {
                const ThingTypePtr type(new ThingType);
                const uint16_t id = appearance.id();
                type->unserializeAppearance(id, static_cast<ThingCategory>(category), appearance);
                m_thingTypes[category][id] = type;
            }
        }
        m_datLoaded = true;
        return true;
    } catch (std::exception& e) {
        g_logger.error(stdext::format("Failed to load '%s' (Appearances): %s", file, e.what()));
        return false;
    }
}

void ThingTypeManager::parseItemType(uint16_t serverId, TiXmlElement* elem)
{
    ItemTypePtr itemType = nullptr;

    bool s;
    int d;

    if (g_game.getClientVersion() < 960) {
        s = serverId > 20000 && serverId < 20100;
        d = 20000;
    } else {
        s = serverId > 30000 && serverId < 30100;
        d = 30000;
    }

    if (s) {
        serverId -= d;
        itemType = ItemTypePtr(new ItemType);
        itemType->setServerId(serverId);
        addItemType(itemType);
    } else
        itemType = getItemType(serverId);

    itemType->setName(elem->Attribute("name"));
    for (TiXmlElement* attrib = elem->FirstChildElement(); attrib; attrib = attrib->NextSiblingElement()) {
        std::string key = attrib->Attribute("key");
        if (key.empty())
            continue;

        stdext::tolower(key);
        if (key == "description")
            itemType->setDesc(attrib->Attribute("value"));
        else if (key == "weapontype")
            itemType->setCategory(ItemCategoryWeapon);
        else if (key == "ammotype")
            itemType->setCategory(ItemCategoryAmmunition);
        else if (key == "armor")
            itemType->setCategory(ItemCategoryArmor);
        else if (key == "charges")
            itemType->setCategory(ItemCategoryCharges);
        else if (key == "type") {
            std::string value = attrib->Attribute("value");
            stdext::tolower(value);

            if (value == "key")
                itemType->setCategory(ItemCategoryKey);
            else if (value == "magicfield")
                itemType->setCategory(ItemCategoryMagicField);
            else if (value == "teleport")
                itemType->setCategory(ItemCategoryTeleport);
            else if (value == "door")
                itemType->setCategory(ItemCategoryDoor);
        }
    }
}

void ThingTypeManager::addItemType(const ItemTypePtr& itemType)
{
    const uint16_t id = itemType->getServerId();
    if (unlikely(id >= m_itemTypes.size()))
        m_itemTypes.resize(id + 1, m_nullItemType);
    m_itemTypes[id] = itemType;
}

const ItemTypePtr& ThingTypeManager::findItemTypeByClientId(uint16_t id)
{
    if (id == 0 || id >= m_reverseItemTypes.size())
        return m_nullItemType;

    if (m_reverseItemTypes[id])
        return m_reverseItemTypes[id];
    return m_nullItemType;
}

const ItemTypePtr& ThingTypeManager::findItemTypeByName(const std::string& name)
{
    for (const ItemTypePtr& it : m_itemTypes)
        if (it->getName() == name)
            return it;
    return m_nullItemType;
}

ItemTypeList ThingTypeManager::findItemTypesByName(const std::string& name)
{
    ItemTypeList ret;
    for (const ItemTypePtr& it : m_itemTypes)
        if (it->getName() == name)
            ret.push_back(it);
    return ret;
}

ItemTypeList ThingTypeManager::findItemTypesByString(const std::string& name)
{
    ItemTypeList ret;
    for (const ItemTypePtr& it : m_itemTypes)
        if (it->getName().find(name) != std::string::npos)
            ret.push_back(it);
    return ret;
}

const ThingTypePtr& ThingTypeManager::getThingType(uint16_t id, ThingCategory category)
{
    if (category >= ThingLastCategory || id >= m_thingTypes[category].size()) {
        g_logger.error(stdext::format("invalid thing type client id %d in category %d", id, category));
        return m_nullThingType;
    }
    return m_thingTypes[category][id];
}

const ItemTypePtr& ThingTypeManager::getItemType(uint16_t id)
{
    if (id >= m_itemTypes.size() || m_itemTypes[id] == m_nullItemType) {
        g_logger.error(stdext::format("invalid thing type, server id: %d", id));
        return m_nullItemType;
    }
    return m_itemTypes[id];
}

ThingTypeList ThingTypeManager::findThingTypeByAttr(ThingAttr attr, ThingCategory category)
{
    ThingTypeList ret;
    for (const auto& type : m_thingTypes[category])
        if (type->hasAttr(attr))
            ret.push_back(type);
    return ret;
}

ItemTypeList ThingTypeManager::findItemTypeByCategory(ItemCategory category)
{
    ItemTypeList ret;
    for (const ItemTypePtr& type : m_itemTypes)
        if (type->getCategory() == category)
            ret.push_back(type);
    return ret;
}

const ThingTypeList& ThingTypeManager::getThingTypes(ThingCategory category)
{
    if (category < ThingLastCategory)
        return m_thingTypes[category];

    stdext::throw_exception(stdext::format("invalid thing type category %d", category));
}

/* vim: set ts=4 sw=4 et: */
