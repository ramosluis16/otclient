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

#pragma once

#include "lightview.h"
#include <framework/core/inputevent.h>
#include <framework/graphics/declarations.h>
#include <framework/graphics/paintershaderprogram.h>
#include <framework/luaengine/luaobject.h>

struct AwareRange
{
    uint8_t left, top, right, bottom;
    uint8_t horizontal() { return left + right + 1; }
    uint8_t vertical() { return top + bottom + 1; }
};

struct MapPosInfo
{
    Rect rect, srcRect;
    Point drawOffset;
    float horizontalStretchFactor,
        verticalStretchFactor;

    bool isInRange(const Position& pos, bool ignoreZ = false) const
    {
        return camera.isInRange(pos, awareRange.left - 1, awareRange.right - 2, awareRange.top - 1, awareRange.bottom - 2, ignoreZ);
    }

    bool isInRangeEx(const Position& pos, bool ignoreZ = false)  const
    {
        return camera.isInRange(pos, awareRange.left, awareRange.right, awareRange.top, awareRange.bottom, ignoreZ);
    }

private:
    Position camera;
    AwareRange awareRange;

    friend class MapView;
};

// @bindclass
class MapView : public LuaObject
{
public:
    enum FloorViewMode
    {
        NORMAL,
        FADE,
        LOCKED,
        ALWAYS,
        ALWAYS_WITH_TRANSPARENCY
    };

    enum AntialiasingMode :uint8_t
    {
        ANTIALIASING_DISABLED,
        ANTIALIASING_ENABLED,
        ANTIALIASING_SMOOTH_RETRO
    };

    MapView();
    ~MapView() override;
    void draw(const Rect& rect);

    // floor visibility related
    uint8_t getLockedFirstVisibleFloor() { return m_lockedFirstVisibleFloor; }
    uint8_t getCachedFirstVisibleFloor() { return m_cachedFirstVisibleFloor; }
    uint8_t getCachedLastVisibleFloor() { return m_cachedLastVisibleFloor; }
    uint8_t getTileSize() { return m_tileSize; }

    void lockFirstVisibleFloor(uint8_t firstVisibleFloor);
    void unlockFirstVisibleFloor();

    // map dimension related
    Size getVisibleDimension() { return m_visibleDimension; }
    void setVisibleDimension(const Size& visibleDimension);

    // view mode related
    FloorViewMode getFloorViewMode() { return m_floorViewMode; }
    void setFloorViewMode(FloorViewMode viewMode);

    // camera related
    CreaturePtr getFollowingCreature() { return m_followingCreature; }
    void followCreature(const CreaturePtr& creature);
    bool isFollowingCreature() { return m_followingCreature && m_follow; }

    Position getCameraPosition();
    void setCameraPosition(const Position& pos);

    void setMinimumAmbientLight(float intensity) { m_minimumAmbientLight = intensity; updateLight(); }
    float getMinimumAmbientLight() { return m_minimumAmbientLight; }

    void setShadowFloorIntensity(float intensity) { m_shadowFloorIntensity = intensity; updateLight(); }
    float getShadowFloorIntensity() { return m_shadowFloorIntensity; }

    // drawing related
    void setDrawTexts(bool enable) { m_drawTexts = enable; }
    bool isDrawingTexts() { return m_drawTexts; }

    void setDrawNames(bool enable) { m_drawNames = enable; }
    bool isDrawingNames() { return m_drawNames; }

    void setDrawHealthBars(bool enable) { m_drawHealthBars = enable; }
    bool isDrawingHealthBars() { return m_drawHealthBars; }

    void setDrawLights(bool enable);
    bool isDrawingLights() { return m_lightView && m_lightView->isDark(); }

    void setLimitVisibleDimension(bool v) { m_limitVisibleDimension = v; }
    bool isLimitedVisibleDimension() { return m_limitVisibleDimension; }

    void setDrawManaBar(bool enable) { m_drawManaBar = enable; }
    bool isDrawingManaBar() { return m_drawManaBar; }

    void move(int32_t x, int32_t y);

    void setShader(const PainterShaderProgramPtr& shader, float fadein, float fadeout);
    PainterShaderProgramPtr getShader() { return m_shader; }

    Position getPosition(const Point& point, const Size& mapSize);

    MapViewPtr asMapView() { return static_self_cast<MapView>(); }

    void resetLastCamera() { m_lastCameraPosition = {}; }

    std::vector<CreaturePtr> getSpectators(bool multiFloor = false);
    std::vector<CreaturePtr> getSightSpectators(bool multiFloor = false);

    bool isInRange(const Position& pos, bool ignoreZ = false)
    {
        return getCameraPosition().isInRange(pos, m_posInfo.awareRange.left - 1, m_posInfo.awareRange.right - 2, m_posInfo.awareRange.top - 1, m_posInfo.awareRange.bottom - 2, ignoreZ);
    }

    bool isInRangeEx(const Position& pos, bool ignoreZ = false)
    {
        return getCameraPosition().isInRange(pos, m_posInfo.awareRange.left, m_posInfo.awareRange.right, m_posInfo.awareRange.top, m_posInfo.awareRange.bottom, ignoreZ);
    }

    TilePtr getTopTile(Position tilePos);

    void setCrosshairTexture(const std::string& texturePath);
    void setAntiAliasingMode(AntialiasingMode mode);

    void onMouseMove(const Position& mousePos, bool isVirtualMove = false);
    void onKeyRelease(const InputEvent& inputEvent);

    void setLastMousePosition(const Position& mousePos) { m_mousePosition = mousePos; }
    const Position& getLastMousePosition() { return m_mousePosition; }

    void setDrawHighlightTarget(const bool enable) { m_drawHighlightTarget = enable; }

    void setFloorFading(uint16_t value) { m_floorFading = value; }

protected:
    void onGlobalLightChange(const Light& light);
    void onFloorChange(uint8_t floor, uint8_t previousFloor);
    void onTileUpdate(const Position& pos, const ThingPtr& thing, Otc::Operation operation);
    void onMapCenterChange(const Position& newPos, const Position& oldPos);
    void onCameraMove(const Point& offset);
    void onFadeInFinished();

    friend class Map;
    friend class Tile;
    friend class LightView;

private:
    struct MapObject
    {
        std::vector<TilePtr> shades, tiles;
        void clear() { shades.clear(); tiles.clear(); }
    };

    struct Crosshair
    {
        bool positionChanged = false;
        Position position;
        TexturePtr texture;
    };

    void updateGeometry(const Size& visibleDimension);
    void updateVisibleTiles();
    void requestUpdateVisibleTiles() { m_updateVisibleTiles = true; }
    void requestUpdateMapPosInfo() { m_posInfo.rect = {}; }

    uint8_t calcFirstVisibleFloor(bool checkLimitsFloorsView);
    uint8_t calcLastVisibleFloor();

    void updateLight();
    void updateViewportDirectionCache();
    void drawFloor();
    void drawText();

    void updateViewport(const Otc::Direction dir = Otc::InvalidDirection) { m_viewport = m_viewPortDirection[dir]; }

    bool canFloorFade() { return m_floorViewMode == FADE && m_floorFading; }

    float getFadeLevel(uint8_t z)
    {
        if (!canFloorFade()) return 1.f;

        float fading = std::clamp<float>(static_cast<float>(m_fadingFloorTimers[z].elapsed_millis()) / static_cast<float>(m_floorFading), 0.f, 1.f);
        if (z < m_cachedFirstVisibleFloor)
            fading = 1.0 - fading;
        return fading;
    }

    Rect calcFramebufferSource(const Size& destSize);

    Point transformPositionTo2D(const Position& position, const Position& relativePosition)
    {
        return {
            (m_virtualCenterOffset.x + (position.x - relativePosition.x) - (relativePosition.z - position.z)) * m_tileSize,
                     (m_virtualCenterOffset.y + (position.y - relativePosition.y) - (relativePosition.z - position.z)) * m_tileSize
        };
    }

    int8_t m_lockedFirstVisibleFloor{ -1 };

    uint8_t m_cachedFirstVisibleFloor{ SEA_FLOOR },
        m_cachedLastVisibleFloor{ SEA_FLOOR },
        m_tileSize{ SPRITE_SIZE },
        m_floorMin{ 0 },
        m_floorMax{ 0 },
        m_antiAliasingMode;

    uint16_t m_floorFading = 500;

    float m_minimumAmbientLight{ 0 },
        m_fadeInTime{ 0 },
        m_fadeOutTime{ 0 },
        m_shadowFloorIntensity{ 0 },
        m_scaleFactor{ 1.f },
        m_lastFadeLevel{ 1.f };

    Rect m_rectDimension;

    Size m_drawDimension, m_visibleDimension;

    Point m_virtualCenterOffset, m_moveOffset;

    Position m_customCameraPosition,
        m_lastCameraPosition,
        m_mousePosition;

    std::array<AwareRange, Otc::InvalidDirection + 1> m_viewPortDirection;
    AwareRange m_viewport;

    bool
        m_limitVisibleDimension{ true },
        m_updateVisibleTiles{ true },
        m_resetCoveredCache{ true },
        m_shaderSwitchDone{ true },
        m_drawHealthBars{ true },
        m_drawManaBar{ true },
        m_drawTexts{ true },
        m_drawNames{ true },
        m_smooth{ true },
        m_follow{ true };

    bool
        m_autoViewMode{ false },
        m_drawViewportEdge{ false },
        m_drawHighlightTarget{ false },
        m_shiftPressed{ false };

    std::array<MapObject, MAX_Z + 1> m_cachedVisibleTiles;

    stdext::timer m_fadingFloorTimers[MAX_Z + 1];

    PainterShaderProgramPtr m_shader, m_nextShader;
    LightViewPtr m_lightView;
    CreaturePtr m_followingCreature;

    MapPosInfo m_posInfo;
    FloorViewMode m_floorViewMode{ NORMAL };

    Timer m_fadeTimer;

    TilePtr m_lastHighlightTile;
    TexturePtr m_crosshairTexture;
    EffectPtr m_crosshairEffect;
};
