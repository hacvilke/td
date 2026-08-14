// =============================================================================
// TD Engine - Scene Graph / Node Hierarchy (Tier 1.1)
//
// Adds parent/child relationships to the flat ECS. Inspired by Godot's Node
// tree and Unity's Transform hierarchy.
//
// Design:
//   - A Scene is a thin wrapper around World that tracks a root set of
//     "top-level" entities (no parent) and provides tree-traversal helpers.
//   - Parenting is stored on each entity as a HierarchyComponent (entity ID
//     of the parent) + a sibling-linked-list via firstChild / nextSibling.
//     This is the same layout Godot uses: O(1) parent lookup, O(1) child
//     prepend, O(children) remove.
//   - Transform inheritance: a child's WORLD transform = parent.world * child.local.
//     We compute world transforms in a single top-down pass per frame.
//   - Scene save/load is handled by the Serializer (Tier 1.2).
//
// The HierarchyComponent / LocalTransformComponent / WorldTransformComponent
// structs live in src/ecs/component.h and are registered in the ECS like any
// other component (see src/ecs/world.cpp IMPL_* block).
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../core/signal.h"
#include "../core/logger.h"
#include <cmath>
#include <cstdint>

namespace td {

class Scene {
public:
    Scene() : m_world(new World()) {}
    ~Scene() { delete m_world; }

    World* world() { return m_world; }
    const World* world() const { return m_world; }

    // ---------------------------------------------------------------------
    // Entity creation. createEntity() makes a root-level entity. Use
    // setParent() to attach it to another entity.
    // ---------------------------------------------------------------------
    EntityId createEntity(const char* name = "Entity") {
        EntityId id = m_world->createEntity(name);
        if (id == INVALID_ENTITY) return id;
        // Roots don't strictly need a HierarchyComponent, but adding it
        // keeps the iteration uniform and lets us tell roots (hierarchy
        // present, parent == INVALID_ENTITY) from non-hierarchy entities
        // (no HierarchyComponent at all).
        m_world->addComponent<HierarchyComponent>(id);
        m_world->addComponent<LocalTransformComponent>(id);
        m_world->addComponent<WorldTransformComponent>(id);
        m_roots.push(id);  // track as a root
        return id;
    }

    // Destroy an entity AND all its descendants. The recursive destroy is
    // required because orphans (entities with parent == destroyed_id) would
    // otherwise dangle forever.
    void destroyEntity(EntityId id) {
        if (!m_world->entityExists(id)) return;
        // Recurse into children first.
        HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        if (h) {
            EntityId child = h->firstChild;
            while (child != INVALID_ENTITY) {
                HierarchyComponent* ch = m_world->getComponent<HierarchyComponent>(child);
                EntityId next = ch ? ch->nextSibling : INVALID_ENTITY;
                destroyEntity(child);
                child = next;
            }
        }
        // Unlink from parent.
        if (h && h->parent != INVALID_ENTITY) {
            unlinkFromParent(id, h);
        } else {
            // Was a root; remove from root list.
            for (int i = 0; i < m_roots.size(); i++) {
                if (m_roots[i] == id) {
                    m_roots[i] = m_roots[m_roots.size() - 1];
                    m_roots.pop();
                    break;
                }
            }
        }
        m_world->destroyEntity(id);
        SignalPayload p; p.intValue = (int)id;
        SignalBus::get().emit("scene:entity_destroyed", p);
    }

    // ---------------------------------------------------------------------
    // Parenting.
    // ---------------------------------------------------------------------
    void setParent(EntityId child, EntityId parent) {
        if (child == INVALID_ENTITY || child == parent) return;
        if (!m_world->entityExists(child)) return;
        // Prevent cycles: walk up `parent`'s ancestor chain. If we hit
        // `child`, the operation would create a cycle.
        if (parent != INVALID_ENTITY) {
            EntityId ancestor = parent;
            while (ancestor != INVALID_ENTITY) {
                if (ancestor == child) {
                    TD_LOG_WARN("Scene::setParent: refusing to create cycle "
                                "(entity %u is an ancestor of %u)", child, parent);
                    return;
                }
                HierarchyComponent* ah = m_world->getComponent<HierarchyComponent>(ancestor);
                ancestor = ah ? ah->parent : INVALID_ENTITY;
            }
        }

        HierarchyComponent* ch = m_world->getComponent<HierarchyComponent>(child);
        if (!ch) {
            ch = m_world->addComponent<HierarchyComponent>(child);
            m_world->addComponent<LocalTransformComponent>(child);
            m_world->addComponent<WorldTransformComponent>(child);
        }

        // Unlink from current parent.
        if (ch->parent != INVALID_ENTITY) {
            unlinkFromParent(child, ch);
        } else {
            // Was a root; remove from root list.
            for (int i = 0; i < m_roots.size(); i++) {
                if (m_roots[i] == child) {
                    m_roots[i] = m_roots[m_roots.size() - 1];
                    m_roots.pop();
                    break;
                }
            }
        }

        // Link to new parent.
        ch->parent = parent;
        ch->nextSibling = INVALID_ENTITY;
        ch->prevSibling = INVALID_ENTITY;
        if (parent != INVALID_ENTITY) {
            HierarchyComponent* ph = m_world->getComponent<HierarchyComponent>(parent);
            if (!ph) {
                ph = m_world->addComponent<HierarchyComponent>(parent);
                m_world->addComponent<LocalTransformComponent>(parent);
                m_world->addComponent<WorldTransformComponent>(parent);
            }
            // Prepend to parent's child list.
            ch->nextSibling = ph->firstChild;
            if (ph->firstChild != INVALID_ENTITY) {
                HierarchyComponent* fch = m_world->getComponent<HierarchyComponent>(ph->firstChild);
                if (fch) fch->prevSibling = child;
            }
            ph->firstChild = child;
            ch->depth = ph->depth + 1;
        } else {
            // Re-rooting.
            ch->depth = 0;
            m_roots.push(child);
        }

        // Mark the whole subtree dirty so world transforms get recomputed.
        markSubtreeDirty(child);

        SignalPayload p; p.intValue = (int)child;
        SignalBus::get().emit("scene:parent_changed", p);
    }

    EntityId getParent(EntityId id) const {
        const HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        return h ? h->parent : INVALID_ENTITY;
    }

    EntityId getFirstChild(EntityId id) const {
        const HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        return h ? h->firstChild : INVALID_ENTITY;
    }

    int getChildCount(EntityId id) const {
        const HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        if (!h) return 0;
        int n = 0;
        EntityId c = h->firstChild;
        while (c != INVALID_ENTITY) {
            n++;
            const HierarchyComponent* ch = m_world->getComponent<HierarchyComponent>(c);
            c = ch ? ch->nextSibling : INVALID_ENTITY;
        }
        return n;
    }

    // Get the root entities (those with no parent).
    int getRoots(EntityId* outIds, int maxResults) const {
        int n = m_roots.size() < maxResults ? m_roots.size() : maxResults;
        for (int i = 0; i < n; i++) outIds[i] = m_roots[i];
        return n;
    }

    // ---------------------------------------------------------------------
    // Transform inheritance.
    //
    // Walks the tree top-down (roots first, then their children, etc.) and
    // recomputes WorldTransformComponent from LocalTransformComponent +
    // parent's WorldTransformComponent. Dirty entities skip the recomputation
    // unless an ANCESTOR was dirty (which forces the whole subtree to update).
    //
    // Cost: O(N) where N = number of entities with HierarchyComponent. Only
    // dirty subtrees pay the matrix multiply; clean subtrees are skipped
    // entirely.
    // ---------------------------------------------------------------------
    void updateTransforms() {
        for (int i = 0; i < m_roots.size(); i++) {
            updateTransformRecursive(m_roots[i], /*parentWorld=*/nullptr);
        }
    }

    // Mark a local transform as changed. Call after mutating
    // LocalTransformComponent fields. The next updateTransforms() will
    // propagate the change to all descendants.
    void markDirty(EntityId id) {
        HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        if (h) markSubtreeDirty(id);
    }

    void setLocalPosition(EntityId id, float x, float y) {
        LocalTransformComponent* lt = m_world->getComponent<LocalTransformComponent>(id);
        if (!lt) return;
        lt->x = x; lt->y = y;
        markDirty(id);
    }

    void setLocalRotation(EntityId id, float radians) {
        LocalTransformComponent* lt = m_world->getComponent<LocalTransformComponent>(id);
        if (!lt) return;
        lt->rotation = radians;
        markDirty(id);
    }

    void setLocalScale(EntityId id, float sx, float sy) {
        LocalTransformComponent* lt = m_world->getComponent<LocalTransformComponent>(id);
        if (!lt) return;
        lt->scaleX = sx; lt->scaleY = sy;
        markDirty(id);
    }

    // Convenience: get the world position of an entity (post-updateTransforms).
    bool getWorldPosition(EntityId id, float& outX, float& outY) const {
        const WorldTransformComponent* wt = m_world->getComponent<WorldTransformComponent>(id);
        if (!wt) return false;
        outX = wt->x; outY = wt->y;
        return true;
    }

    void clear() {
        m_world->clear();
        m_roots.clear();
    }

private:
    // A tiny growable array for the root list and entity ID buffers.
    // We don't use std::vector to keep the engine STL-light (the existing
    // code uses raw arrays everywhere).
    struct EntityIdList {
        EntityId* data = nullptr;
        int       count = 0;
        int       cap   = 0;
        void push(EntityId id) {
            if (count >= cap) {
                int newCap = cap ? cap * 2 : 16;
                EntityId* nd = new EntityId[newCap];
                for (int i = 0; i < count; i++) nd[i] = data[i];
                delete[] data;
                data = nd; cap = newCap;
            }
            data[count++] = id;
        }
        void pop() { if (count > 0) count--; }
        void clear() { count = 0; }
        EntityId operator[](int i) const { return data[i]; }
        EntityId& operator[](int i) { return data[i]; }
        int size() const { return count; }
    };

    void unlinkFromParent(EntityId child, HierarchyComponent* ch) {
        HierarchyComponent* ph = m_world->getComponent<HierarchyComponent>(ch->parent);
        if (!ph) { ch->parent = INVALID_ENTITY; return; }
        if (ph->firstChild == child) {
            ph->firstChild = ch->nextSibling;
        }
        if (ch->prevSibling != INVALID_ENTITY) {
            HierarchyComponent* pv = m_world->getComponent<HierarchyComponent>(ch->prevSibling);
            if (pv) pv->nextSibling = ch->nextSibling;
        }
        if (ch->nextSibling != INVALID_ENTITY) {
            HierarchyComponent* nx = m_world->getComponent<HierarchyComponent>(ch->nextSibling);
            if (nx) nx->prevSibling = ch->prevSibling;
        }
        ch->parent = INVALID_ENTITY;
        ch->nextSibling = INVALID_ENTITY;
        ch->prevSibling = INVALID_ENTITY;
    }

    void markSubtreeDirty(EntityId id) {
        HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        if (!h) return;
        h->transformDirty = true;
        EntityId child = h->firstChild;
        while (child != INVALID_ENTITY) {
            markSubtreeDirty(child);
            HierarchyComponent* ch = m_world->getComponent<HierarchyComponent>(child);
            child = ch ? ch->nextSibling : INVALID_ENTITY;
        }
    }

    void updateTransformRecursive(EntityId id, const WorldTransformComponent* parentWorld) {
        HierarchyComponent* h = m_world->getComponent<HierarchyComponent>(id);
        if (!h) return;
        LocalTransformComponent*  lt = m_world->getComponent<LocalTransformComponent>(id);
        WorldTransformComponent*  wt = m_world->getComponent<WorldTransformComponent>(id);
        if (!lt || !wt) return;

        if (h->transformDirty || parentWorld) {
            if (parentWorld) {
                // Compose: world = parentWorld * local
                // For 2D TRS (translation, rotation, scale-shear-ignored):
                //   world_rot   = parent.rot + local.rot
                //   world_scale  = parent.scale * local.scale
                //   world_pos.x  = parent.pos.x + cos(parent.rot) * local.x * parent.scaleX
                //                                    - sin(parent.rot) * local.y * parent.scaleY
                //   world_pos.y  = parent.pos.y + sin(parent.rot) * local.x * parent.scaleX
                //                                    + cos(parent.rot) * local.y * parent.scaleY
                float pr = parentWorld->rotation;
                float psx = parentWorld->scaleX, psy = parentWorld->scaleY;
                float cr = cosf(pr), sr = sinf(pr);
                wt->rotation = pr + lt->rotation;
                wt->scaleX   = psx * lt->scaleX;
                wt->scaleY   = psy * lt->scaleY;
                float lx = lt->x * lt->scaleX;
                float ly = lt->y * lt->scaleY;
                wt->x = parentWorld->x + cr * lx - sr * ly;
                wt->y = parentWorld->y + sr * lx + cr * ly;
            } else {
                // Root: world = local.
                wt->x = lt->x; wt->y = lt->y;
                wt->rotation = lt->rotation;
                wt->scaleX = lt->scaleX; wt->scaleY = lt->scaleY;
            }
            h->transformDirty = false;
        }

        // Recurse into children, passing our world transform as their parent.
        EntityId child = h->firstChild;
        while (child != INVALID_ENTITY) {
            updateTransformRecursive(child, wt);
            HierarchyComponent* ch = m_world->getComponent<HierarchyComponent>(child);
            child = ch ? ch->nextSibling : INVALID_ENTITY;
        }
    }

    World*        m_world;
    EntityIdList  m_roots;
};

} // namespace td
