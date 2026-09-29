/**************************************************************************/
/*  voxel_grid.h                                                          */
/**************************************************************************/
/*  Goblin Kitchen fork: goblin_voxel module.                             */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"
#include "scene/resources/mesh.h"

// A box of voxels, one byte each: 0 is empty, 1-255 index the game's material palette. Positions are
// grid-local metres: voxel (x, y, z) fills [x, x + 1) * voxel_size on each axis, so y = 0 is the floor.
// The heavy work of voxel objects (trees, rocks, structures) lives here; game code decides what it means.
class GKVoxelGrid : public RefCounted {
	GDCLASS(GKVoxelGrid, RefCounted);

	Vector3i size;
	real_t voxel_size = 0.25;
	Vector<uint8_t> cells;

	_FORCE_INLINE_ int _index(int p_x, int p_y, int p_z) const { return p_x + size.x * (p_y + size.y * p_z); }
	_FORCE_INLINE_ bool _inside(int p_x, int p_y, int p_z) const {
		return p_x >= 0 && p_y >= 0 && p_z >= 0 && p_x < size.x && p_y < size.y && p_z < size.z;
	}
	_FORCE_INLINE_ uint8_t _at(int p_x, int p_y, int p_z) const {
		return _inside(p_x, p_y, p_z) ? cells[_index(p_x, p_y, p_z)] : 0;
	}
	// Greedy quads over all faces between solid and empty. With p_by_material, faces of different
	// materials don't merge. Calls p_emit(corners[4], normal, material) for each quad.
	template <typename F>
	void _greedy_quads(bool p_by_material, F p_emit) const;

protected:
	static void _bind_methods();

public:
	void create(const Vector3i &p_size);
	Vector3i get_size() const { return size; }
	void set_voxel_size(real_t p_size) { voxel_size = p_size; }
	real_t get_voxel_size() const { return voxel_size; }

	int get_voxel(const Vector3i &p_pos) const;
	void set_voxel(const Vector3i &p_pos, int p_material);
	// The voxel containing a grid-local point.
	Vector3i voxel_at(const Vector3 &p_local) const;
	// The centre of a voxel, grid-local.
	Vector3 voxel_center(const Vector3i &p_pos) const;

	void fill_box(const Vector3i &p_from, const Vector3i &p_to, int p_material);
	// Voxels whose centres are within p_radius (metres) of a grid-local point. p_only_empty keeps what's
	// there (for layering shapes).
	void fill_sphere(const Vector3 &p_center, real_t p_radius, int p_material, bool p_only_empty = false);
	// A capsule-like line of spheres from a to b, radius lerped (trunks, branches, roots).
	void fill_line(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius_from, real_t p_radius_to, int p_material);

	int count() const;
	bool is_empty() const { return count() == 0; }
	// material -> number of voxels.
	Dictionary count_by_material() const;

	// Removes voxels whose centres are within p_radius of a grid-local point and whose material's
	// hardness (p_hardness[material], 0 if missing) is at most p_max_hardness. Returns material -> count.
	Dictionary remove_sphere(const Vector3 &p_center, real_t p_radius, int p_max_hardness, const PackedByteArray &p_hardness);

	// Splits off the parts no longer connected (6-neighbour) to what holds the object up: with
	// p_anchor_floor, voxels in the bottom layer (y = 0); otherwise the largest part. The split parts
	// are removed from this grid and returned as [{"grid": GKVoxelGrid (cropped), "offset": Vector3i}].
	TypedArray<Dictionary> split_loose(bool p_anchor_floor);

	// Mass, centre of mass and rotational inertia (diagonal, about the centre, grid axes) from a density
	// per material in kg/m^3: {"mass": float, "center": Vector3, "inertia": Vector3}. Mass 0 when empty.
	Dictionary mass_properties(const PackedFloat32Array &p_density) const;

	// The lowest and highest solid voxel (min inclusive, max exclusive), or zeros if empty.
	AABB get_solid_bounds() const;

	// Greedy-meshed surface with a flat colour per material (p_colors[material]); null when empty.
	Ref<ArrayMesh> build_mesh(const PackedColorArray &p_colors) const;
	// Surface triangles for a ConcavePolygonShape3D (static objects).
	PackedVector3Array build_collision_faces() const;
	// Solid space as few boxes as greedily found (for moving pieces, which can't use concave shapes).
	// With p_solid (a flag per material), materials flagged 0 (leaves) get no boxes.
	TypedArray<AABB> build_boxes(const PackedByteArray &p_solid = PackedByteArray()) const;
	// Removes voxels whose centres are below a grid-local plane (on its back side) and soft enough, as
	// remove_sphere. For a piece landing: what sank into the ground is crushed.
	Dictionary remove_below(const Plane &p_plane, int p_max_hardness, const PackedByteArray &p_hardness);

	// Text save form: "sx sy sz voxel_size|count*material count*material ..." (runs, x fastest).
	String to_text() const;
	Error from_text(const String &p_text);
};
