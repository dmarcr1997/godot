/**************************************************************************/
/*  voxel_grid.h                                                          */
/**************************************************************************/
/*  Goblin Kitchen fork: goblin_voxel module.                             */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
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
	// The box of voxels changed since take_dirty() was last called (inclusive), if `dirty`.
	Vector3i dirty_lo;
	Vector3i dirty_hi;
	bool dirty = false;

	_FORCE_INLINE_ void _touch(const Vector3i &p_lo, const Vector3i &p_hi) {
		dirty_lo = dirty ? dirty_lo.min(p_lo) : p_lo;
		dirty_hi = dirty ? dirty_hi.max(p_hi) : p_hi;
		dirty = true;
	}

	_FORCE_INLINE_ int _index(int p_x, int p_y, int p_z) const { return p_x + size.x * (p_y + size.y * p_z); }
	_FORCE_INLINE_ bool _inside(int p_x, int p_y, int p_z) const {
		return p_x >= 0 && p_y >= 0 && p_z >= 0 && p_x < size.x && p_y < size.y && p_z < size.z;
	}
	_FORCE_INLINE_ uint8_t _at(int p_x, int p_y, int p_z) const {
		return _inside(p_x, p_y, p_z) ? cells[_index(p_x, p_y, p_z)] : 0;
	}
	// Greedy quads over the faces between solid and empty of the voxels in [p_from, p_to): a face belongs
	// to its solid voxel, so the regions of a grid split into blocks give every face exactly once, and
	// what lies outside the region still hides faces. With p_by_material, faces of different materials
	// don't merge. Calls p_emit(corners[4], normal, material) for each quad.
	// The cells (indices, in the order found) of the biggest part that reaches the floor.
	void _standing_cells(const PackedByteArray &p_solid, LocalVector<int32_t> &r_cells) const;
	template <typename F>
	void _greedy_quads(bool p_by_material, const Vector3i &p_from, const Vector3i &p_to, F p_emit) const;

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
	// With p_solid (a flag per material), only solid voxels hold things together: parts are found through
	// solid voxels, and each non-solid voxel (leaves) goes with the solid part it grows from (nearest
	// through other leaves); leaves touching no wood are a part of their own.
	TypedArray<Dictionary> split_loose(bool p_anchor_floor, const PackedByteArray &p_solid = PackedByteArray());

	// What a standing structure stands on: its main body is the biggest part (6-neighbour, through solid
	// voxels only when p_solid is given) that reaches the floor (y = 0). Returns {"voxels": how many it
	// has (0: nothing stands), "columns": Array of Vector2i (x, z) where it meets the floor, "com": Vector3
	// (grid-local, the mean of its voxel centres), "centre": Vector2 (grid-local x, z metres, the middle
	// of its columns)}.
	Dictionary standing_body(const PackedByteArray &p_solid = PackedByteArray()) const;
	// Takes that main body out of the grid: {"grid": GKVoxelGrid (cropped), "offset": Vector3i}, or an
	// empty dictionary if nothing stands. (A structure toppling: its body becomes a falling piece.)
	Dictionary take_standing_body(const PackedByteArray &p_solid = PackedByteArray());

	// Mass, centre of mass and rotational inertia (diagonal, about the centre, grid axes) from a density
	// per material in kg/m^3: {"mass": float, "center": Vector3, "inertia": Vector3}. Mass 0 when empty.
	Dictionary mass_properties(const PackedFloat32Array &p_density) const;

	// The lowest and highest solid voxel (min inclusive, max exclusive), or zeros if empty.
	AABB get_solid_bounds() const;

	// Greedy-meshed surface with a flat colour per material (p_colors[material]); null when empty.
	Ref<ArrayMesh> build_mesh(const PackedColorArray &p_colors) const;
	// Surface triangles for a ConcavePolygonShape3D (static objects).
	PackedVector3Array build_collision_faces() const;
	// The same for the voxels in [p_from, p_to) only (positions stay grid-local): an object kept as blocks
	// rebuilds just the blocks a break touched.
	Ref<ArrayMesh> build_mesh_region(const PackedColorArray &p_colors, const Vector3i &p_from, const Vector3i &p_to) const;
	PackedVector3Array build_collision_faces_region(const Vector3i &p_from, const Vector3i &p_to) const;
	// The box of voxels changed since the last call, as [lowest, highest] (Vector3i, inclusive), or an
	// empty array if nothing changed. Every call that writes voxels widens it; reading it clears it.
	Array take_dirty();
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
