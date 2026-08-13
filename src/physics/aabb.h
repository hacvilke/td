#pragma once
#include "../core/math/vec2.h"

namespace td {

struct AABB {
    float minX, minY, maxX, maxY;
    
    AABB() : minX(0), minY(0), maxX(0), maxY(0) {}
    AABB(float minX_, float minY_, float maxX_, float maxY_)
        : minX(minX_), minY(minY_), maxX(maxX_), maxY(maxY_) {}
    
    static AABB fromCenter(float cx, float cy, float width, float height) {
        float hw = width * 0.5f;
        float hh = height * 0.5f;
        return AABB(cx - hw, cy - hh, cx + hw, cy + hh);
    }
    
    static AABB fromMinSize(float x, float y, float width, float height) {
        return AABB(x, y, x + width, y + height);
    }
    
    bool overlaps(const AABB& other) const;
    bool contains(const AABB& other) const;
    bool containsPoint(float x, float y) const;
    
    AABB intersection(const AABB& other) const;
    AABB merged(const AABB& other) const;
    
    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
    Vec2 center() const { return Vec2((minX + maxX) * 0.5f, (minY + maxY) * 0.5f); }
    Vec2 min() const { return Vec2(minX, minY); }
    Vec2 max() const { return Vec2(maxX, maxY); }
    Vec2 size() const { return Vec2(width(), height()); }
    float area() const { return width() * height(); }
    float perimeter() const { return 2.0f * (width() + height()); }
    
    void expand(float amount);
    void translate(float dx, float dy);
    void setCenter(float cx, float cy);
    void setSize(float w, float h);
    
    bool isValid() const { return maxX > minX && maxY > minY; }
};

// 3D version
struct AABB3D {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    
    AABB3D() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    AABB3D(float minX_, float minY_, float minZ_, float maxX_, float maxY_, float maxZ_)
        : minX(minX_), minY(minY_), minZ(minZ_), maxX(maxX_), maxY(maxY_), maxZ(maxZ_) {}
    
    bool overlaps(const AABB3D& other) const;
    bool containsPoint(float x, float y, float z) const;
    
    Vec3 center() const;
    Vec3 size() const;
    float volume() const;
};

} // namespace td
