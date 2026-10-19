// model_library.h - the Scene Builder's model library: the ready-made models in the models/ folder (scans and sculpts from public 3D test collections as .obj, and everyday objects, furniture and props as .glb from
// the Khronos sample assets, Poly Haven and Kenney),
// with a display name each, and the rule that sets a new one down: scaled to a handy size, centred on the drop point, standing on the floor.
// Qt-free; docs/MODELS.md says where each model comes from and what that source's licence asks.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace model_library {

struct Entry {
	const char* stem;      // the file name without ".obj" (models/<stem>.obj, models/thumbnails/<stem>.png)
	const char* name;      // what the menu shows
	const char* source;    // where it comes from, in a few words
	int kiloTriangles;     // roughly, to warn about heavy ones
	bool bundled;          // shipped inside the macOS and Windows installers (small, and clear to redistribute); the others come with a source checkout
	const char* file = nullptr;   // where the model is under models/ when it is not <stem>.obj: a glTF model, "gltf/<stem>.glb" (added with the glTF importer, at its real size)
};

// The model's file, relative to the models/ folder.
inline std::string fileOf(const Entry& e) { return e.file ? std::string(e.file) : std::string(e.stem) + ".obj"; }
inline bool isGltf(const Entry& e) { return e.file != nullptr; }

inline const std::vector<Entry>& catalog() {
	static const std::vector<Entry> all = {
		{"spot", "Spot (cow)", "Keenan Crane, CC0", 6, true},
		{"suzanne", "Suzanne (monkey)", "Blender", 1, true},
		{"teapot", "Utah teapot", "Martin Newell, public domain", 6, true},
		{"stanford-bunny", "Stanford bunny", "Stanford 3D Scanning Repository", 69, false},
		{"armadillo", "Armadillo", "Stanford 3D Scanning Repository", 100, false},
		{"happy-buddha", "Happy Buddha", "Stanford 3D Scanning Repository", 99, false},
		{"lucy", "Lucy (angel)", "Stanford 3D Scanning Repository", 100, false},
		{"xyzrgb_dragon", "Dragon", "Stanford 3D Scanning Repository (XYZ RGB)", 250, false},
		{"cow", "Cow", "public 3D test-model collection", 6, false},
		{"horse", "Horse", "public 3D test-model collection", 97, false},
		{"ogre", "Ogre", "public 3D test-model collection", 124, false},
		{"beast", "Beast", "public 3D test-model collection", 32, false},
		{"homer", "Homer", "public 3D test-model collection", 12, false},
		{"cheburashka", "Cheburashka", "public 3D test-model collection", 13, false},
		{"beetle", "Beetle (car, light)", "public 3D test-model collection", 2, false},
		{"beetle-alt", "Beetle (car, detailed)", "public 3D test-model collection", 39, false},
		{"nefertiti", "Nefertiti (bust)", "public 3D test-model collection", 100, false},
		{"max-planck", "Max Planck (bust)", "public 3D test-model collection", 100, false},
		{"bimba", "Bimba (bust)", "public 3D test-model collection", 225, false},
		{"igea", "Igea (head)", "public 3D test-model collection", 269, false},
		{"fandisk", "Fandisk (mechanical part)", "public 3D test-model collection", 13, false},
		{"rocker-arm", "Rocker arm", "public 3D test-model collection", 20, false},
		{"antique-camera", "Antique camera", "Khronos glTF Sample Assets, CC0", 20, false, "gltf/antique-camera.glb"},
		{"avocado", "Avocado", "Khronos glTF Sample Assets, CC0", 1, false, "gltf/avocado.glb"},
		{"barramundi-fish", "Barramundi fish", "Khronos glTF Sample Assets, CC0", 4, false, "gltf/barramundi-fish.glb"},
		{"boom-box", "Boom box", "Khronos glTF Sample Assets, CC0", 6, false, "gltf/boom-box.glb"},
		{"sheen-chair", "Chair (sheen)", "Khronos glTF Sample Assets, CC0", 40, false, "gltf/sheen-chair.glb"},
		{"corset", "Corset", "Khronos glTF Sample Assets, CC0", 18, false, "gltf/corset.glb"},
		{"lantern-khronos", "Lantern (old)", "Khronos glTF Sample Assets, CC0", 5, false, "gltf/lantern-khronos.glb"},
		{"toy-car", "Toy car", "Khronos glTF Sample Assets, CC0", 109, false, "gltf/toy-car.glb"},
		{"water-bottle", "Water bottle", "Khronos glTF Sample Assets, CC0", 5, false, "gltf/water-bottle.glb"},
		{"candle-holder", "Glass candle holder", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 4, false, "gltf/candle-holder.glb"},
		{"shoe", "Shoe", "Khronos glTF Sample Assets, CC BY 4.0 (Shopify)", 23, false, "gltf/shoe.glb"},
		{"silk-pouf", "Silk pouf", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 81, false, "gltf/silk-pouf.glb"},
		{"sunglasses", "Sunglasses", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 13, false, "gltf/sunglasses.glb"},
		{"traffic-cone", "Traffic cone", "Khronos glTF Sample Assets, CC BY 4.0 (hinndia)", 17, false, "gltf/traffic-cone.glb"},
		{"velvet-sofa", "Velvet sofa", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 4, false, "gltf/velvet-sofa.glb"},
		{"alarm-clock", "Alarm clock", "Poly Haven, CC0", 9, false, "gltf/alarm-clock.glb"},
		{"apple", "Apple", "Poly Haven, CC0", 7, false, "gltf/apple.glb"},
		{"barrel", "Barrel", "Poly Haven, CC0", 3, false, "gltf/barrel.glb"},
		{"camera-01", "Camera 01", "Poly Haven, CC0", 27, false, "gltf/camera-01.glb"},
		{"chess-set", "Chess set", "Poly Haven, CC0", 77, false, "gltf/chess-set.glb"},
		{"cat-statue", "Concrete cat statue", "Poly Haven, CC0", 12, false, "gltf/cat-statue.glb"},
		{"horse-statue", "Horse statue", "Poly Haven, CC0", 22, false, "gltf/horse-statue.glb"},
		{"lantern-01", "Lantern 01", "Poly Haven, CC0", 34, false, "gltf/lantern-01.glb"},
		{"marble-bust", "Marble bust", "Poly Haven, CC0", 17, false, "gltf/marble-bust.glb"},
		{"arm-chair", "Modern arm chair", "Poly Haven, CC0", 9, false, "gltf/arm-chair.glb"},
		{"potted-plant", "Potted plant", "Poly Haven, CC0", 70, false, "gltf/potted-plant.glb"},
		{"rubber-duck", "Rubber duck", "Poly Haven, CC0", 4, false, "gltf/rubber-duck.glb"},
		{"sofa-02", "Sofa 02", "Poly Haven, CC0", 3, false, "gltf/sofa-02.glb"},
		{"tea-set", "Tea set", "Poly Haven, CC0", 40, false, "gltf/tea-set.glb"},
		{"television-01", "Television 01", "Poly Haven, CC0", 2, false, "gltf/television-01.glb"},
		{"wine-bottles", "Wine bottles", "Poly Haven, CC0", 29, false, "gltf/wine-bottles.glb"},
		{"wooden-table", "Wooden table", "Poly Haven, CC0", 1, false, "gltf/wooden-table.glb"},
		{"bathtub", "Bathtub", "Kenney Furniture Kit, CC0", 1, false, "gltf/bathtub.glb"},
		{"bookcase", "Bookcase", "Kenney Furniture Kit, CC0", 1, false, "gltf/bookcase.glb"},
		{"coffee-table", "Coffee table", "Kenney Furniture Kit, CC0", 1, false, "gltf/coffee-table.glb"},
		{"design-chair", "Design chair", "Kenney Furniture Kit, CC0", 1, false, "gltf/design-chair.glb"},
		{"desk", "Desk", "Kenney Furniture Kit, CC0", 1, false, "gltf/desk.glb"},
		{"double-bed", "Double bed", "Kenney Furniture Kit, CC0", 1, false, "gltf/double-bed.glb"},
		{"floor-lamp", "Floor lamp", "Kenney Furniture Kit, CC0", 1, false, "gltf/floor-lamp.glb"},
		{"fridge", "Fridge", "Kenney Furniture Kit, CC0", 1, false, "gltf/fridge.glb"},
		{"stove", "Kitchen stove", "Kenney Furniture Kit, CC0", 1, false, "gltf/stove.glb"},
		{"laptop", "Laptop", "Kenney Furniture Kit, CC0", 1, false, "gltf/laptop.glb"},
		{"lounge-sofa", "Lounge sofa (low poly)", "Kenney Furniture Kit, CC0", 1, false, "gltf/lounge-sofa.glb"},
		{"chair-modern", "Modern chair (low poly)", "Kenney Furniture Kit, CC0", 1, false, "gltf/chair-modern.glb"},
		{"plant-lowpoly", "Potted plant (low poly)", "Kenney Furniture Kit, CC0", 1, false, "gltf/plant-lowpoly.glb"},
		{"radio", "Radio", "Kenney Furniture Kit, CC0", 1, false, "gltf/radio.glb"},
		{"round-rug", "Round rug", "Kenney Furniture Kit, CC0", 1, false, "gltf/round-rug.glb"},
		{"speaker", "Speaker", "Kenney Furniture Kit, CC0", 1, false, "gltf/speaker.glb"},
		{"teddy-bear", "Teddy bear", "Kenney Furniture Kit, CC0", 1, false, "gltf/teddy-bear.glb"},
		{"toaster", "Toaster", "Kenney Furniture Kit, CC0", 1, false, "gltf/toaster.glb"},
		{"trashcan", "Trash can", "Kenney Furniture Kit, CC0", 1, false, "gltf/trashcan.glb"},
		{"tv-vintage", "Vintage television", "Kenney Furniture Kit, CC0", 1, false, "gltf/tv-vintage.glb"},
		{"car-concept", "Car concept (scene)", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 1, false, "gltf/car-concept.glb"},
		{"broken-window", "Broken window (scene)", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 1, false, "gltf/broken-window.glb"},
		{"vase-with-flowers", "Glass vase with flowers (scene)", "Khronos glTF Sample Assets, CC0", 1, false, "gltf/vase-with-flowers.glb"},
		{"dish-with-olives", "Iridescent dish with olives (scene)", "Khronos glTF Sample Assets, CC BY 4.0 (Eric Chadwick)", 1, false, "gltf/dish-with-olives.glb"},
	};
	return all;
}

inline const Entry* findEntry(const char* stem) {
	for (const Entry& e : catalog())
		if (std::string(e.stem) == stem) return &e;
	return nullptr;
}

// Where a library model goes: `scale` makes its largest side `targetSize` scene units; `offset` is the Scene Builder position that puts the model's
// bounding-box centre over the drop point (x and z) and its lowest point on the floor (y = 0). lo and hi are the file's own bounding box.
struct Placement {
	double scale = 1.0;
	double offset[3] = {0.0, 0.0, 0.0};
};

inline Placement placeOnFloor(const double lo[3], const double hi[3], double dropX, double dropZ, double targetSize = 1.6) {
	Placement p;
	const double largest = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
	p.scale = largest > 1e-12 ? targetSize / largest : 1.0;
	p.offset[0] = dropX - 0.5 * (lo[0] + hi[0]) * p.scale;
	p.offset[1] = -lo[1] * p.scale;
	p.offset[2] = dropZ - 0.5 * (lo[2] + hi[2]) * p.scale;
	return p;
}

}  // namespace model_library
