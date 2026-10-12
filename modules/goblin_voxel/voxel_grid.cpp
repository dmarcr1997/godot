/**************************************************************************/
/*  voxel_grid.cpp                                                        */
/**************************************************************************/
/*  Goblin Kitchen fork: goblin_voxel module.                             */
/**************************************************************************/

#include "voxel_grid.h"

#include "core/object/class_db.h"
#include "core/templates/local_vector.h"

void GKVoxelGrid::create(const Vector3i &p_size) {
	ERR_FAIL_COND_MSG(p_size.x < 1 || p_size.y < 1 || p_size.z < 1, "GKVoxelGrid: size must be at least 1 on each axis.");
	ERR_FAIL_COND_MSG((int64_t)p_size.x * p_size.y * p_size.z > 16 * 1024 * 1024, "GKVoxelGrid: too many voxels.");
	size = p_size;
	cells.resize(size.x * size.y * size.z);
	cells.fill(0);
	dirty = false;
	_touch(Vector3i(), size - Vector3i(1, 1, 1));
}

int GKVoxelGrid::get_voxel(const Vector3i &p_pos) const {
	return _at(p_pos.x, p_pos.y, p_pos.z);
}

void GKVoxelGrid::set_voxel(const Vector3i &p_pos, int p_material) {
	if (_inside(p_pos.x, p_pos.y, p_pos.z)) {
		cells.write[_index(p_pos.x, p_pos.y, p_pos.z)] = (uint8_t)CLAMP(p_material, 0, 255);
		_touch(p_pos, p_pos);
	}
}

Vector3i GKVoxelGrid::voxel_at(const Vector3 &p_local) const {
	return Vector3i(Math::floor(p_local.x / voxel_size), Math::floor(p_local.y / voxel_size), Math::floor(p_local.z / voxel_size));
}

Vector3 GKVoxelGrid::voxel_center(const Vector3i &p_pos) const {
	return (Vector3(p_pos) + Vector3(0.5, 0.5, 0.5)) * voxel_size;
}

void GKVoxelGrid::fill_box(const Vector3i &p_from, const Vector3i &p_to, int p_material) {
	const Vector3i lo = p_from.min(p_to).max(Vector3i());
	const Vector3i hi = p_from.max(p_to).min(size - Vector3i(1, 1, 1));
	if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) {
		return;
	}
	_touch(lo, hi);
	uint8_t *w = cells.ptrw();
	for (int z = lo.z; z <= hi.z; z++) {
		for (int y = lo.y; y <= hi.y; y++) {
			for (int x = lo.x; x <= hi.x; x++) {
				w[_index(x, y, z)] = (uint8_t)CLAMP(p_material, 0, 255);
			}
		}
	}
}

void GKVoxelGrid::fill_sphere(const Vector3 &p_center, real_t p_radius, int p_material, bool p_only_empty) {
	fill_line(p_center, p_center, p_radius, p_radius, p_only_empty ? -CLAMP(p_material, 0, 255) - 1 : p_material);
}

void GKVoxelGrid::fill_line(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius_from, real_t p_radius_to, int p_material) {
	// A negative material is the fill_sphere "only empty" form: -(material + 1).
	const bool only_empty = p_material < 0;
	const uint8_t mat = (uint8_t)CLAMP(only_empty ? -p_material - 1 : p_material, 0, 255);
	const real_t r_max = MAX(p_radius_from, p_radius_to);
	const Vector3i lo = voxel_at(p_from.min(p_to) - Vector3(r_max, r_max, r_max)).max(Vector3i());
	const Vector3i hi = voxel_at(p_from.max(p_to) + Vector3(r_max, r_max, r_max)).min(size - Vector3i(1, 1, 1));
	const Vector3 seg = p_to - p_from;
	const real_t seg_len2 = seg.length_squared();
	if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) {
		return;
	}
	_touch(lo, hi);
	uint8_t *w = cells.ptrw();
	for (int z = lo.z; z <= hi.z; z++) {
		for (int y = lo.y; y <= hi.y; y++) {
			for (int x = lo.x; x <= hi.x; x++) {
				const Vector3 c = voxel_center(Vector3i(x, y, z));
				const real_t t = seg_len2 > 0 ? CLAMP((c - p_from).dot(seg) / seg_len2, 0, 1) : 0;
				const real_t r = Math::lerp(p_radius_from, p_radius_to, t);
				if ((p_from + seg * t).distance_squared_to(c) <= r * r) {
					uint8_t &cell = w[_index(x, y, z)];
					if (!only_empty || cell == 0) {
						cell = mat;
					}
				}
			}
		}
	}
}

int GKVoxelGrid::count() const {
	int n = 0;
	for (uint8_t c : cells) {
		n += c != 0;
	}
	return n;
}

Dictionary GKVoxelGrid::count_by_material() const {
	int counts[256] = {};
	for (uint8_t c : cells) {
		counts[c]++;
	}
	Dictionary out;
	for (int m = 1; m < 256; m++) {
		if (counts[m] > 0) {
			out[m] = counts[m];
		}
	}
	return out;
}

Dictionary GKVoxelGrid::remove_sphere(const Vector3 &p_center, real_t p_radius, int p_max_hardness, const PackedByteArray &p_hardness) {
	int counts[256] = {};
	const Vector3 r3(p_radius, p_radius, p_radius);
	const Vector3i lo = voxel_at(p_center - r3).max(Vector3i());
	const Vector3i hi = voxel_at(p_center + r3).min(size - Vector3i(1, 1, 1));
	uint8_t *w = cells.ptrw();
	for (int z = lo.z; z <= hi.z; z++) {
		for (int y = lo.y; y <= hi.y; y++) {
			for (int x = lo.x; x <= hi.x; x++) {
				uint8_t &cell = w[_index(x, y, z)];
				if (cell == 0 || voxel_center(Vector3i(x, y, z)).distance_squared_to(p_center) > p_radius * p_radius) {
					continue;
				}
				const int hardness = cell < p_hardness.size() ? p_hardness[cell] : 0;
				if (hardness <= p_max_hardness) {
					counts[cell]++;
					cell = 0;
					_touch(Vector3i(x, y, z), Vector3i(x, y, z));
				}
			}
		}
	}
	Dictionary out;
	for (int m = 1; m < 256; m++) {
		if (counts[m] > 0) {
			out[m] = counts[m];
		}
	}
	return out;
}

TypedArray<Dictionary> GKVoxelGrid::split_loose(bool p_anchor_floor, const PackedByteArray &p_solid) {
	TypedArray<Dictionary> out;
	const int total = cells.size();
	LocalVector<int32_t> label;
	label.resize(total);
	for (int i = 0; i < total; i++) {
		label[i] = -1;
	}
	struct Part {
		int voxels = 0;
		int solid = 0;
		bool on_floor = false;
		Vector3i lo;
		Vector3i hi;
	};
	LocalVector<Part> parts;
	LocalVector<int32_t> stack;
	const uint8_t *r = cells.ptr();
	const bool masked = !p_solid.is_empty();
	auto solid = [&](int i) { return r[i] != 0 && (!masked || r[i] >= p_solid.size() || p_solid[r[i]] != 0); };
	static const int dirs6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	// Parts through solid voxels first (all voxels when unmasked).
	for (int pass = 0; pass < 2; pass++) {
	if (pass == 1) {
		// Leaves go with the part they grow from: spread each part's label out through non-solid voxels,
		// breadth first from every solid voxel at once.
		LocalVector<int32_t> frontier;
		for (int i = 0; i < total; i++) {
			if (label[i] != -1) {
				frontier.push_back(i);
			}
		}
		for (uint32_t head = 0; head < frontier.size(); head++) {
			const int i = frontier[head];
			const int x = i % size.x;
			const int y = (i / size.x) % size.y;
			const int z = i / (size.x * size.y);
			for (const auto &d : dirs6) {
				const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
				if (!_inside(nx, ny, nz)) {
					continue;
				}
				const int n = _index(nx, ny, nz);
				if (r[n] != 0 && label[n] == -1) {
					label[n] = label[i];
					Part &part = parts[label[i]];
					part.voxels++;
					part.lo = part.lo.min(Vector3i(nx, ny, nz));
					part.hi = part.hi.max(Vector3i(nx, ny, nz));
					frontier.push_back(n);
				}
			}
		}
	}
	for (int start = 0; start < total; start++) {
		if (r[start] == 0 || label[start] != -1 || (pass == 0 && !solid(start))) {
			continue;
		}
		const int id = parts.size();
		Part part;
		part.lo = Vector3i(INT32_MAX, INT32_MAX, INT32_MAX);
		part.hi = Vector3i(-1, -1, -1);
		label[start] = id;
		stack.push_back(start);
		while (!stack.is_empty()) {
			const int i = stack[stack.size() - 1];
			stack.resize(stack.size() - 1);
			const int x = i % size.x;
			const int y = (i / size.x) % size.y;
			const int z = i / (size.x * size.y);
			part.voxels++;
			part.solid += solid(i) ? 1 : 0;
			part.on_floor = part.on_floor || y == 0;
			part.lo = part.lo.min(Vector3i(x, y, z));
			part.hi = part.hi.max(Vector3i(x, y, z));
			for (const auto &d : dirs6) {
				const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
				if (!_inside(nx, ny, nz)) {
					continue;
				}
				const int n = _index(nx, ny, nz);
				if (r[n] != 0 && label[n] == -1 && (pass == 1 || solid(n))) {
					label[n] = id;
					stack.push_back(n);
				}
			}
		}
		parts.push_back(part);
	}
	}
	if (parts.size() == 0) {
		return out;
	}
	// Without a floor, the part with the most that holds together (solid voxels) stays: a trunk, not the
	// leafy crown that came off it.
	int largest = 0;
	for (uint32_t p = 1; p < parts.size(); p++) {
		if (parts[p].solid > parts[largest].solid || (parts[p].solid == parts[largest].solid && parts[p].voxels > parts[largest].voxels)) {
			largest = p;
		}
	}
	uint8_t *w = cells.ptrw();
	for (uint32_t p = 0; p < parts.size(); p++) {
		const bool keep = p_anchor_floor ? parts[p].on_floor : (int)p == largest;
		if (keep) {
			continue;
		}
		const Part &part = parts[p];
		Ref<GKVoxelGrid> piece;
		piece.instantiate();
		piece->voxel_size = voxel_size;
		piece->create(part.hi - part.lo + Vector3i(1, 1, 1));
		_touch(part.lo, part.hi);
		uint8_t *pw = piece->cells.ptrw();
		for (int z = part.lo.z; z <= part.hi.z; z++) {
			for (int y = part.lo.y; y <= part.hi.y; y++) {
				for (int x = part.lo.x; x <= part.hi.x; x++) {
					const int i = _index(x, y, z);
					if (label[i] == (int32_t)p) {
						pw[piece->_index(x - part.lo.x, y - part.lo.y, z - part.lo.z)] = w[i];
						w[i] = 0;
					}
				}
			}
		}
		Dictionary d;
		d["grid"] = piece;
		d["offset"] = part.lo;
		out.push_back(d);
	}
	return out;
}

void GKVoxelGrid::_standing_cells(const PackedByteArray &p_solid, LocalVector<int32_t> &r_cells) const {
	r_cells.clear();
	const uint8_t *r = cells.ptr();
	const bool masked = !p_solid.is_empty();
	auto holds = [&](int p_index) {
		const uint8_t m = r[p_index];
		return m != 0 && (!masked || (m < p_solid.size() && p_solid[m] != 0));
	};
	LocalVector<uint8_t> seen;
	seen.resize(cells.size());
	memset(seen.ptr(), 0, seen.size());
	LocalVector<int32_t> part;
	// The neighbours in the order the game's own search used: +x, -x, +y, -y, -z, +z.
	const int step[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
	for (int z = 0; z < size.z; z++) {
		for (int x = 0; x < size.x; x++) {
			const int start = _index(x, 0, z);
			if (seen[start] || !holds(start)) {
				continue;
			}
			part.clear();
			part.push_back(start);
			seen[start] = 1;
			for (uint32_t i = 0; i < part.size(); i++) {
				const int c = part[i];
				const int cx = c % size.x;
				const int cy = (c / size.x) % size.y;
				const int cz = c / (size.x * size.y);
				for (int d = 0; d < 6; d++) {
					const int nx = cx + step[d][0];
					const int ny = cy + step[d][1];
					const int nz = cz + step[d][2];
					if (!_inside(nx, ny, nz)) {
						continue;
					}
					const int n = _index(nx, ny, nz);
					if (!seen[n] && holds(n)) {
						seen[n] = 1;
						part.push_back(n);
					}
				}
			}
			if (part.size() > r_cells.size()) {
				r_cells = part;
			}
		}
	}
}

Dictionary GKVoxelGrid::standing_body(const PackedByteArray &p_solid) const {
	LocalVector<int32_t> body;
	_standing_cells(p_solid, body);
	Dictionary out;
	TypedArray<Vector2i> columns;
	Vector3 sum;
	Vector2 centre;
	for (const int32_t c : body) {
		const Vector3i v(c % size.x, (c / size.x) % size.y, c / (size.x * size.y));
		sum += voxel_center(v);
		if (v.y == 0) {
			columns.push_back(Vector2i(v.x, v.z));
			centre += (Vector2(v.x, v.z) + Vector2(0.5, 0.5)) * voxel_size;
		}
	}
	out["voxels"] = (int)body.size();
	out["columns"] = columns;
	out["com"] = body.is_empty() ? Vector3() : sum / body.size();
	out["centre"] = columns.is_empty() ? Vector2() : centre / columns.size();
	return out;
}

Dictionary GKVoxelGrid::take_standing_body(const PackedByteArray &p_solid) {
	LocalVector<int32_t> body;
	_standing_cells(p_solid, body);
	Dictionary out;
	if (body.is_empty()) {
		return out;
	}
	Vector3i lo(size);
	Vector3i hi(-1, -1, -1);
	for (const int32_t c : body) {
		const Vector3i v(c % size.x, (c / size.x) % size.y, c / (size.x * size.y));
		lo = lo.min(v);
		hi = hi.max(v);
	}
	Ref<GKVoxelGrid> piece;
	piece.instantiate();
	piece->voxel_size = voxel_size;
	piece->create(hi - lo + Vector3i(1, 1, 1));
	uint8_t *pw = piece->cells.ptrw();
	uint8_t *w = cells.ptrw();
	for (const int32_t c : body) {
		const Vector3i v(c % size.x, (c / size.x) % size.y, c / (size.x * size.y));
		pw[piece->_index(v.x - lo.x, v.y - lo.y, v.z - lo.z)] = w[c];
		w[c] = 0;
	}
	_touch(lo, hi);
	out["grid"] = piece;
	out["offset"] = lo;
	return out;
}

Dictionary GKVoxelGrid::mass_properties(const PackedFloat32Array &p_density) const {
	const real_t volume = voxel_size * voxel_size * voxel_size;
	double mass = 0;
	Vector3 moment;
	// First pass: mass and centre.
	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				const uint8_t m = cells[_index(x, y, z)];
				if (m == 0) {
					continue;
				}
				const double mm = (m < p_density.size() ? p_density[m] : 0.0) * volume;
				mass += mm;
				moment += voxel_center(Vector3i(x, y, z)) * mm;
			}
		}
	}
	Dictionary out;
	out["mass"] = mass;
	out["center"] = mass > 0 ? moment / mass : Vector3();
	out["inertia"] = Vector3();
	if (mass <= 0) {
		return out;
	}
	const Vector3 c = moment / mass;
	// Second pass: inertia of each voxel as a point plus its own cube term (m * s^2 / 6 per axis).
	Vector3 inertia;
	const real_t cube = voxel_size * voxel_size / 6.0;
	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				const uint8_t m = cells[_index(x, y, z)];
				if (m == 0) {
					continue;
				}
				const real_t mm = (m < p_density.size() ? p_density[m] : 0.0) * volume;
				const Vector3 d = voxel_center(Vector3i(x, y, z)) - c;
				inertia += Vector3(d.y * d.y + d.z * d.z + cube, d.x * d.x + d.z * d.z + cube, d.x * d.x + d.y * d.y + cube) * mm;
			}
		}
	}
	out["inertia"] = inertia;
	return out;
}

AABB GKVoxelGrid::get_solid_bounds() const {
	Vector3i lo(INT32_MAX, INT32_MAX, INT32_MAX);
	Vector3i hi(-1, -1, -1);
	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				if (cells[_index(x, y, z)] != 0) {
					lo = lo.min(Vector3i(x, y, z));
					hi = hi.max(Vector3i(x, y, z));
				}
			}
		}
	}
	if (hi.x < 0) {
		return AABB();
	}
	return AABB(Vector3(lo) * voxel_size, Vector3(hi - lo + Vector3i(1, 1, 1)) * voxel_size);
}

template <typename F>
void GKVoxelGrid::_greedy_quads(bool p_by_material, const Vector3i &p_from, const Vector3i &p_to, F p_emit) const {
	const Vector3i from = p_from.max(Vector3i());
	const Vector3i to = p_to.min(size);
	if (from.x >= to.x || from.y >= to.y || from.z >= to.z) {
		return;
	}
	const int lo[3] = { from.x, from.y, from.z };
	const int dims[3] = { to.x - from.x, to.y - from.y, to.z - from.z };
	// An empty region has no faces of its own (a felled tree leaves most of its blocks empty).
	bool any = false;
	const uint8_t *r = cells.ptr();
	for (int z = from.z; z < to.z && !any; z++) {
		for (int y = from.y; y < to.y && !any; y++) {
			const uint8_t *row = r + _index(from.x, y, z);
			for (int x = 0; x < dims[0]; x++) {
				if (row[x] != 0) {
					any = true;
					break;
				}
			}
		}
	}
	if (!any) {
		return;
	}
	LocalVector<int> mask;
	for (int d = 0; d < 3; d++) {
		const int u = (d + 1) % 3;
		const int v = (d + 2) % 3;
		int x[3] = { 0, 0, 0 };
		int q[3] = { 0, 0, 0 };
		q[d] = 1;
		mask.resize(dims[u] * dims[v]);
		for (x[d] = lo[d] - 1; x[d] < lo[d] + dims[d];) {
			// Faces between layer x[d] and x[d] + 1: positive when the solid side is below (normal +d).
			// Only faces whose solid voxel is inside the region are this region's.
			const bool a_in = x[d] >= lo[d];
			const bool b_in = x[d] + 1 < lo[d] + dims[d];
			int n = 0;
			for (x[v] = lo[v]; x[v] < lo[v] + dims[v]; x[v]++) {
				for (x[u] = lo[u]; x[u] < lo[u] + dims[u]; x[u]++) {
					const int a = _at(x[0], x[1], x[2]);
					const int b = _at(x[0] + q[0], x[1] + q[1], x[2] + q[2]);
					int m = 0;
					if ((a != 0) != (b != 0) && (a != 0 ? a_in : b_in)) {
						const int mat = p_by_material ? (a != 0 ? a : b) : 1;
						m = a != 0 ? mat : -mat;
					}
					mask[n++] = m;
				}
			}
			x[d]++;
			n = 0;
			for (int j = 0; j < dims[v]; j++) {
				for (int i = 0; i < dims[u];) {
					const int c = mask[n];
					if (c == 0) {
						i++;
						n++;
						continue;
					}
					int w = 1;
					while (i + w < dims[u] && mask[n + w] == c) {
						w++;
					}
					int h = 1;
					for (bool grow = true; grow && j + h < dims[v];) {
						for (int k = 0; k < w; k++) {
							if (mask[n + k + h * dims[u]] != c) {
								grow = false;
								break;
							}
						}
						if (grow) {
							h++;
						}
					}
					x[u] = lo[u] + i;
					x[v] = lo[v] + j;
					const Vector3 base(x[0], x[1], x[2]);
					Vector3 du;
					Vector3 dv;
					du[u] = w;
					dv[v] = h;
					const Vector3 corners[4] = { base * voxel_size, (base + du) * voxel_size, (base + du + dv) * voxel_size, (base + dv) * voxel_size };
					Vector3 normal;
					normal[d] = c > 0 ? 1 : -1;
					p_emit(corners, normal, c > 0 ? c : -c);
					for (int l = 0; l < h; l++) {
						for (int k = 0; k < w; k++) {
							mask[n + k + l * dims[u]] = 0;
						}
					}
					i += w;
					n += w;
				}
			}
		}
	}
}

// The four corners in Godot's front-face order (clockwise seen from the front).
static void _front_order(const Vector3 p_in[4], const Vector3 &p_normal, Vector3 r_out[4]) {
	const bool flip = (p_in[1] - p_in[0]).cross(p_in[2] - p_in[0]).dot(p_normal) > 0;
	r_out[0] = p_in[0];
	r_out[1] = flip ? p_in[3] : p_in[1];
	r_out[2] = p_in[2];
	r_out[3] = flip ? p_in[1] : p_in[3];
}

Ref<ArrayMesh> GKVoxelGrid::build_mesh(const PackedColorArray &p_colors) const {
	return build_mesh_region(p_colors, Vector3i(), size);
}

PackedVector3Array GKVoxelGrid::build_collision_faces() const {
	return build_collision_faces_region(Vector3i(), size);
}

Array GKVoxelGrid::take_dirty() {
	Array out;
	if (dirty) {
		out.push_back(dirty_lo);
		out.push_back(dirty_hi);
		dirty = false;
	}
	return out;
}

Ref<ArrayMesh> GKVoxelGrid::build_mesh_region(const PackedColorArray &p_colors, const Vector3i &p_from, const Vector3i &p_to) const {
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedInt32Array indices;
	_greedy_quads(true, p_from, p_to, [&](const Vector3 p_corners[4], const Vector3 &p_normal, int p_material) {
		Vector3 c[4];
		_front_order(p_corners, p_normal, c);
		const int first = verts.size();
		const Color col = p_material < p_colors.size() ? p_colors[p_material] : Color(1, 0, 1);
		for (int k = 0; k < 4; k++) {
			verts.push_back(c[k]);
			normals.push_back(p_normal);
			colors.push_back(col);
		}
		const int tri[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k : tri) {
			indices.push_back(first + k);
		}
	});
	if (verts.is_empty()) {
		return Ref<ArrayMesh>();
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

PackedVector3Array GKVoxelGrid::build_collision_faces_region(const Vector3i &p_from, const Vector3i &p_to) const {
	PackedVector3Array faces;
	_greedy_quads(false, p_from, p_to, [&](const Vector3 p_corners[4], const Vector3 &p_normal, int p_material) {
		Vector3 c[4];
		_front_order(p_corners, p_normal, c);
		const int tri[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k : tri) {
			faces.push_back(c[k]);
		}
	});
	return faces;
}

Dictionary GKVoxelGrid::remove_below(const Plane &p_plane, int p_max_hardness, const PackedByteArray &p_hardness) {
	int counts[256] = {};
	uint8_t *w = cells.ptrw();
	for (int z = 0; z < size.z; z++) {
		for (int y = 0; y < size.y; y++) {
			for (int x = 0; x < size.x; x++) {
				uint8_t &cell = w[_index(x, y, z)];
				if (cell == 0 || p_plane.distance_to(voxel_center(Vector3i(x, y, z))) >= 0) {
					continue;
				}
				const int hardness = cell < p_hardness.size() ? p_hardness[cell] : 0;
				if (hardness <= p_max_hardness) {
					counts[cell]++;
					cell = 0;
					_touch(Vector3i(x, y, z), Vector3i(x, y, z));
				}
			}
		}
	}
	Dictionary out;
	for (int m = 1; m < 256; m++) {
		if (counts[m] > 0) {
			out[m] = counts[m];
		}
	}
	return out;
}

TypedArray<AABB> GKVoxelGrid::build_boxes(const PackedByteArray &p_solid) const {
	TypedArray<AABB> out;
	LocalVector<uint8_t> used;
	used.resize(cells.size());
	for (uint32_t i = 0; i < used.size(); i++) {
		const uint8_t m = cells[i];
		used[i] = m == 0 || (m < p_solid.size() && p_solid[m] == 0);
	}
	auto free_at = [&](int x, int y, int z) { return !used[_index(x, y, z)]; };
	for (int y = 0; y < size.y; y++) {
		for (int z = 0; z < size.z; z++) {
			for (int x = 0; x < size.x; x++) {
				if (!free_at(x, y, z)) {
					continue;
				}
				int w = 1;
				while (x + w < size.x && free_at(x + w, y, z)) {
					w++;
				}
				int dz = 1;
				for (bool grow = true; grow && z + dz < size.z;) {
					for (int k = 0; k < w; k++) {
						if (!free_at(x + k, y, z + dz)) {
							grow = false;
							break;
						}
					}
					if (grow) {
						dz++;
					}
				}
				int h = 1;
				for (bool grow = true; grow && y + h < size.y;) {
					for (int l = 0; l < dz && grow; l++) {
						for (int k = 0; k < w; k++) {
							if (!free_at(x + k, y + h, z + l)) {
								grow = false;
								break;
							}
						}
					}
					if (grow) {
						h++;
					}
				}
				for (int m = 0; m < h; m++) {
					for (int l = 0; l < dz; l++) {
						for (int k = 0; k < w; k++) {
							used[_index(x + k, y + m, z + l)] = 1;
						}
					}
				}
				out.push_back(AABB(Vector3(x, y, z) * voxel_size, Vector3(w, h, dz) * voxel_size));
			}
		}
	}
	return out;
}

String GKVoxelGrid::to_text() const {
	String out = vformat("%d %d %d %s|", size.x, size.y, size.z, String::num(voxel_size));
	PackedStringArray runs;
	int i = 0;
	const int total = cells.size();
	while (i < total) {
		const uint8_t m = cells[i];
		int n = 1;
		while (i + n < total && cells[i + n] == m) {
			n++;
		}
		runs.push_back(vformat("%d*%d", n, m));
		i += n;
	}
	return out + String(" ").join(runs);
}

Error GKVoxelGrid::from_text(const String &p_text) {
	const PackedStringArray halves = p_text.split("|");
	ERR_FAIL_COND_V_MSG(halves.size() != 2, ERR_PARSE_ERROR, "GKVoxelGrid: expected 'sx sy sz voxel_size|runs'.");
	const PackedStringArray head = halves[0].split(" ", false);
	ERR_FAIL_COND_V_MSG(head.size() != 4, ERR_PARSE_ERROR, "GKVoxelGrid: bad header.");
	const Vector3i s(head[0].to_int(), head[1].to_int(), head[2].to_int());
	ERR_FAIL_COND_V_MSG(s.x < 1 || s.y < 1 || s.z < 1, ERR_PARSE_ERROR, "GKVoxelGrid: bad size.");
	create(s);
	voxel_size = head[3].to_float();
	const int total = cells.size();
	uint8_t *w = cells.ptrw();
	int i = 0;
	for (const String &run : halves[1].split(" ", false)) {
		const int star = run.find_char('*');
		ERR_FAIL_COND_V_MSG(star < 1, ERR_PARSE_ERROR, "GKVoxelGrid: bad run '" + run + "'.");
		const int n = run.substr(0, star).to_int();
		const int m = CLAMP((int)run.substr(star + 1).to_int(), 0, 255);
		ERR_FAIL_COND_V_MSG(n < 1 || i + n > total, ERR_PARSE_ERROR, "GKVoxelGrid: runs don't fit the size.");
		for (int k = 0; k < n; k++) {
			w[i++] = (uint8_t)m;
		}
	}
	ERR_FAIL_COND_V_MSG(i != total, ERR_PARSE_ERROR, "GKVoxelGrid: runs don't fill the size.");
	return OK;
}

void GKVoxelGrid::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create", "size"), &GKVoxelGrid::create);
	ClassDB::bind_method(D_METHOD("get_size"), &GKVoxelGrid::get_size);
	ClassDB::bind_method(D_METHOD("set_voxel_size", "size"), &GKVoxelGrid::set_voxel_size);
	ClassDB::bind_method(D_METHOD("get_voxel_size"), &GKVoxelGrid::get_voxel_size);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size"), "set_voxel_size", "get_voxel_size");
	ClassDB::bind_method(D_METHOD("get_voxel", "pos"), &GKVoxelGrid::get_voxel);
	ClassDB::bind_method(D_METHOD("set_voxel", "pos", "material"), &GKVoxelGrid::set_voxel);
	ClassDB::bind_method(D_METHOD("voxel_at", "local"), &GKVoxelGrid::voxel_at);
	ClassDB::bind_method(D_METHOD("voxel_center", "pos"), &GKVoxelGrid::voxel_center);
	ClassDB::bind_method(D_METHOD("fill_box", "from", "to", "material"), &GKVoxelGrid::fill_box);
	ClassDB::bind_method(D_METHOD("fill_sphere", "center", "radius", "material", "only_empty"), &GKVoxelGrid::fill_sphere, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("fill_line", "from", "to", "radius_from", "radius_to", "material"), &GKVoxelGrid::fill_line);
	ClassDB::bind_method(D_METHOD("count"), &GKVoxelGrid::count);
	ClassDB::bind_method(D_METHOD("is_empty"), &GKVoxelGrid::is_empty);
	ClassDB::bind_method(D_METHOD("count_by_material"), &GKVoxelGrid::count_by_material);
	ClassDB::bind_method(D_METHOD("remove_sphere", "center", "radius", "max_hardness", "hardness"), &GKVoxelGrid::remove_sphere);
	ClassDB::bind_method(D_METHOD("split_loose", "anchor_floor", "solid"), &GKVoxelGrid::split_loose, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("get_solid_bounds"), &GKVoxelGrid::get_solid_bounds);
	ClassDB::bind_method(D_METHOD("mass_properties", "density"), &GKVoxelGrid::mass_properties);
	ClassDB::bind_method(D_METHOD("build_mesh", "colors"), &GKVoxelGrid::build_mesh);
	ClassDB::bind_method(D_METHOD("build_collision_faces"), &GKVoxelGrid::build_collision_faces);
	ClassDB::bind_method(D_METHOD("build_mesh_region", "colors", "from", "to"), &GKVoxelGrid::build_mesh_region);
	ClassDB::bind_method(D_METHOD("build_collision_faces_region", "from", "to"), &GKVoxelGrid::build_collision_faces_region);
	ClassDB::bind_method(D_METHOD("take_dirty"), &GKVoxelGrid::take_dirty);
	ClassDB::bind_method(D_METHOD("standing_body", "solid"), &GKVoxelGrid::standing_body, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("take_standing_body", "solid"), &GKVoxelGrid::take_standing_body, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("build_boxes", "solid"), &GKVoxelGrid::build_boxes, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("remove_below", "plane", "max_hardness", "hardness"), &GKVoxelGrid::remove_below);
	ClassDB::bind_method(D_METHOD("to_text"), &GKVoxelGrid::to_text);
	ClassDB::bind_method(D_METHOD("from_text", "text"), &GKVoxelGrid::from_text);
}
