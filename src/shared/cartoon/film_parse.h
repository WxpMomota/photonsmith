#pragma once
// film_parse.h -- the film file language (film_file.h) and its parser.
// film_file.h -- films made from files: an ordinary pbrt scene animated by a small text file (name.film), with no C++ and no rebuild.
//
// A film file says what to do with a scene over time; the scene is a pbrt file of your own (a Scene Builder scene, an exported one, one you wrote). Parts of the scene that should
// move are marked in the pbrt file with a comment line, `# @anim NAME`, right before an AttributeBegin ... AttributeEnd block:
//
//     # @anim windmill
//     AttributeBegin
//       Translate 0 2 0
//       Shape "trianglemesh" ...
//     AttributeEnd
//
// and the film file moves, turns, scales, lights and shows those blocks, and puts the camera on its own path. The file is plain text, one thing per line, '#' starts a comment
// (the same style as a camera keyframes file, camera_keyframes.h, whose `key` lines it reuses for the camera):
//
//     film     windmill-day                 # the key: letters, digits and '-' (default: the file's name); `cartoon:<key>` and `--video-preset <key>` name the film
//     name     A Day at the Windmill        # what the scene list calls it
//     about    The sails turn, the sun sets, the camera pulls back.   # one sentence for the scene list
//     scene    windmill.pbrt                # the pbrt scene, relative to this file (required)
//     length   12                           # seconds (required)
//     fps      24                           # (default 24)
//     still    6                            # the moment the scene list's picture shows (default: the middle)
//     tags     emissive,dusk                # words from the scene list's vocabulary (cartoons.h); others are ignored
//     ease     1                            # 1 (the default): motions start and stop gently; 0: they move at an even pace (wherever the line is)
//     shutter  0.5                          # motion blur: a moving block is blurred over this share of a frame (0.5, the default: a film camera's 180-degree shutter); 0: sharp
//
//     shot 0 The sails                      # a camera shot that begins at 0 seconds ... (a hard cut at the next shot's start)
//       key  px py pz  tx ty tz  [fov [lensRadius focusDistance]]  [hold S]    # as in a camera keyframes file: where the camera is and what it looks at
//       key  ...
//     shot 6 Pull back
//       key  ...
//       ease camera 0                       # the camera goes through its keys at an even pace (default 1: it starts and stops gently)
//       dissolve 0.8                       # how this shot comes in: `dissolve`, `fade` (to black and up) or `wipe`, over so many seconds centred on its cut (default: a cut)
//     fadeout  1.5                          # the film ends with a fade to black over its last 1.5 s (a `fade` in its first shot fades it up from black)
//
//     turn  windmill about 0 3 0 axis 0 0 1   0 0   12 720   # rotate NAME: `about X Y Z` the point it turns about (default 0 0 0), `axis X Y Z` (default 0 1 0), then pairs: time degrees
//     move  cart   0 -5 0 0   12 5 0 0                      # translate NAME: groups of time x y z
//     grow  flower about 0 0 0   0 0.1   4 1                 # scale NAME about a point: pairs time scale
//     light lamp   0 0   2 1   10 1   12 0                   # the power of the lights in NAME times k: pairs time k (a lamp that fades up and down)
//     show  ghost  3 5   8 9                                 # NAME is there only between these times (any number of pairs); outside them it is left out
//
// Between two keys a value goes smoothly (or evenly with `ease 0`); before the first key it holds the first value and after the last it holds the last. Coordinates are pbrt's own (the
// scene's, not the cartoon toolkit's), and a film with no `shot` lines keeps the scene's own camera. Everything a pbrt file refers to by a relative path (meshes, pictures) is looked up
// beside the pbrt file. A problem in a film file is reported with its line number when the file is loaded (`ray_tracer --film-check FILE`) and the film is left out.
//
// Characters - a film blocked out as a screenplay. A character from the library (characterKinds, film_kinds.h: `nomai-explorer`, `pip`, `person`, `turtle`) is brought in,
// walks and acts (`scale S` on its line sizes a pip or a turtle):
//
//     character  kai nomai-explorer  at 0 0 4  facing 180       # NAME KIND, where it stands (pbrt's x y z; y is the ground) and which way it faces (0: +z, 90: +x)
//     character  ann person  sweater red  hair blond            # ... and the colours of its parts (each kind's: --film-check lists them): a name, r,g,b (0 to 1) or #rrggbb
//     walk       kai  2.0 0.7   0 -2   3 -6                     # NAME, from 2 s at 0.7 m/s, through these points (x z) from where it is
//     walk       kai  9.0 0.7   to 3 -6                         # ... or to a place, finding its own way round the set and whoever stands there (film_paths.h)
//     gesture    kai  "look up" 8.0 9.0 11.0 11.5               # NAME, a pose of its library, eased in from 8 s to 9 s, out from 11 s to 11.5 s (no out times: held)
//     run        kai  4.0 3.0   2 -6                            # as `walk` (points, or `to` a place), running: a longer stride, a lean
//     jump       kai  7.0 to 3 -7  height 0.5                   # NAME, at 7 s, a jump to there: a crouch before it, an arc, a landing that squashes and is heard
//     sit        kai  9.0 height 0.45 until 14                  # NAME sits down at 9 s on a seat that high (0: the ground) and stands up at 14 s
//     gesture    kai  "reach to the wall" 12 13 contact 12.8    # contact: the hand meets something (a touch on the soundtrack)
//     say        kai  14.4 "Look, the first star."  rate 1.1  pitch 2   # NAME, a line it says from 14.4 s: its voice, its mouth and a caption (film_speech.h; `nocaption`)
//     feel       pip  happy 3 3.5 6 6.8  amount 0.8             # NAME, an expression on its face eased in and out (film_faces.h: happy, sad, angry, surprised, ...)
//     look       pip  at ann 2 2.5 8 8.5                        # NAME turns its head and eyes to someone, a prop or a point (`at X Y Z`) and back
//     crowd      folk person 12 in -8 -12 8 -4  seed 3          # NAME KIND COUNT in X0 Z0 X1 Z1: extras of varied looks that wander, pause and turn, each in a cell of its
//                                                               # own (`from T`, `to T`: when they wander); not in the crowding check or an `auto` shot unless named
//     idle       off                                            # (a film line) no idle life: by default a character breathes, shifts its weight, darts its eyes,
//                                                               # turns to whoever speaks and looks at the one it speaks to (film_kinds.h; a `look` line wins)
//
// Props that characters handle - a marked block that is picked up, carried, put down and handed over (it follows the hand that holds it):
//
//     prop       cup  at 0.6 0.82 -1.0                          # a block (`# @anim cup`) a character can pick up: where its handle is in the scene
//     pick       ann  cup 3.0  left                             # NAME reaches for it and takes it at 3.0 s (its usual hand, or `left` / `right`)
//     put        ann  cup 7.5  at 1.2 0.8 -1.5                  # puts it down there at 7.5 s
//     give       ann  cup 9.0  to pip                           # hands it to pip at 9.0 s (their hands meet half way)
//
//     shot 0 Walking
//       follow   kai  behind 3  up 1.7  side 0.6  fov 35          # instead of `key` lines: the camera behind NAME (metres), at a height, to its right, looking at its chest
//     shot 9 Looking up
//       key ...
//       subject  kai                                            # what the shot is about (the film doctor's framing checks); a follow shot is about whom it follows
//     shot 10 Around her
//       orbit    ann  radius 3  degrees 120  height 1.6        # instead of `key` lines, a move about a character: orbit (round them), crane (rise in front of them:
//                                                               # from H1 to H2, distance D), push (in towards them: from D1 to D2), track (beside them as they go: side S)
//       handheld 1                                              # (any shot) a gentle shake, as a hand-held camera
//       focus    ann  4.5 pip                                   # (any shot) the focus on ann, pulled to pip at 4.5 s (`aperture R`, 0.02 when not given)
//     shot 12 Talking
//       auto     pip ann                                        # instead of `key` lines: shots chosen from what happens - a two-shot, over-the-shoulder shots of
//       side     left                                           # each speaker as they speak - all on one side of the line between the first two (`left`: the first on the left)
//
// A film with characters reports its subjects, its footfalls and gesture contacts (Film::events: footsteps on the soundtrack) and its characters' feet (the sliding-feet check).
//
// Props with physics (prop_physics.h) - a marked block that is dropped or thrown and bounces and rolls, or that topples over; each landing is heard (an impact):
//
//     bounce   pebble 6.0  from 0.6 2.4 -3   velocity 0.4 0 -0.2   radius 0.06   # from 6 s: its centre at `from` (where the scene has it), thrown at `velocity`
//     topple   tablet 9.2  pivot 1.4 0 -3.6   axis 0 0 1   height 0.6  depth 0.08   # from 9.2 s it tips over its bottom edge through `pivot` (right-hand turn about `axis`)
//
// (bounce also takes restitution, grip and rolling; topple push and lean: prop_physics.h.)
//
// Dressing the set (film_dressing.h) - things from the nature library on the ground, a sun and sky that follow the time of day, and the set's sound:
//
//     place    tree  at 3 0 -6  scale 1.2  turn 40              # a tree, rock, fern, bush or grass tuft, its foot there (pbrt's x y z)
//     scatter  grass 300 in -8 -10 8 2  scale 0.7 1.3  seed 2   # so many of a kind over a rectangle of the ground (x0 z0 x1 z1; `y` its height)
//     sky      18:30 19:10  south 180  latitude 40              # the sun and sky from 6:30 to 7:10 pm over the film (the scene's own infinite and distant lights are left out)
//     ambience birds 0.6                                        # a sound under the whole film: wind, birds, crickets, water or night, at a gain
//     mute     birds                                            # no birds heard: no birdsong (the trees' or an ambience's), no eagles' cries, no geese, no owl
//     music    none                                             # no music: without a cues file (film_assets/<key>/cues.txt) a film plays its drafted score
//                                                               # (film_score.h, `--film-score`) under its sounds; `music draft` says so
//     terrain  mountains  size 1500  height 320  seed 3         # the land (film_terrain.h): hills, mountains, dunes, canyon or coast, eroded, coloured by height, slope and water,
//                                                               # with far ranges round it (`far off`); `at X Z`, `y Y`, `focus X Z` (finest there; a valley floor round it),
//                                                               # `snow H` (the snow line above y), `erosion E` (0 to 3); on a terrain every height is above the ground
//     water    sea  level 0  waves 1.2  wind 30                 # a sea, lake or clear water at a height (pbrt's y), its waves rolling (the significant height, which way
//                                                               # they run, `choppy C`, `foam off`); on a terrain it fills what lies below its level and laps the shore
//     surf     1.5  period 10  toward 20                       # a swell breaking on the terrain's shore (film_surf.h): it slows, turns to the beach, grows, breaks white;
//                                                               # clear shallows over caustics, foam streaks, spray (the sea's wind when `toward` is not given)
//     river    0 -300  20 -100  0 0  width 12  speed 1.5        # waypoints downstream (pbrt's x z); on a terrain it lies in the lowest ground across its width;
//                                                               # `river from X Z` finds its own way downhill, `river canyon` runs along a canyon's riverbed
//     waterfall at 40 0 -80  width 8  drop 30  facing 180       # the middle of its lip (on a terrain, Y above the ground), how far it drops, which way it pours
//     clouds   cumulus  cover 0.4  base 900  wind 90  speed 6   # clouds you can see round (film_clouds.h): cumulus, towering or wisps; `size W` across, `density D`,
//                                                               # `area A` (the square they are spread over, round `at X Z`), `seed N`; they drift and cast shadows
//                                                               # `clouds deck [thick T]`: one layer to fly through; `fogbank at X Z [depth D] [during T0 T1]`: fog in
//                                                               # the low ground, burning off
//     biome    savanna                                         # the land as a biome (savanna, jungle, desert, tundra, arctic): its ground's colours, its plants, its sound
//     forest   mixed 3000  season autumn                        # conifer, broadleaf, birch, palm, shrub, acacia, kapok, cactus, fern; or mixed (by height), savanna,
//                                                               # jungle, desert or tundra (a biome's mix): so many trees where the land suits them;
//                                                               # `season summer to autumn from T0 to T1` (each tree turning in its own time; `then winter from T2 to T3`
//                                                               # on to the next), `grow T0 T1` (from saplings), `in X0 Z0 X1 Z1`, `below H` (the tree line above the land's
//                                                               # base), `above H`, `steep S`, `scale A B`, `near D` (drawn in full within D m of the camera), `seed N`
//     flowers  poppies 3000  in -20 -20 20 20  bloom 2 10       # poppies, daisies, lupins or sunflowers (turning to the sun), opening each in its own time; `close T0 T1`
//     flock    starlings 300  at 0 80 -200  radius 40           # animals (film_wildlife.h): starlings or fish (flocking round `at`), geese (`from X Y Z to X Y Z`, in
//                                                               # a V), butterflies (`in X0 Z0 X1 Z1`); `speed S`, `during T0 T1`, `seed N`; on a terrain Y is above it
//     herd     deer 12  from -200 0  to 200 -150  speed 1.1     # deer, horses, wolves, lions, zebras, giraffes, elephants, bears or foxes (rigged: film_animals.h)
//                                                              # walking over the land, grazing now and then;
//                                                               # `flee T` (they bolt and gallop away), `start T`, `seed N`; `flock eagles` circle a thermal
//     aurora   green  brightness 1.2  during 4 30               # the sky's spectacles (film_skyfx.h): curtains of light (green, red or violet; `facing D`, `distance M`),
//     lightning 5  at 0 -3000  radius 2000  during 2 20         # strikes with their flash and thunder (`base H`, `seed N`), shooting stars (`meteors COUNT`), and
//     rainbow  0.8  during 6 16                                 # a rainbow opposite the sun (the `sky` line's); at night the sky has its stars, turning with the hours
//     wet      0.8  from 4 to 14                                # weather that changes the land: wet (darker, glossy, puddles in the flat hollows), snowcover (snow on the
//     snowcover 0.7  from 0 to 20                               # ground level enough to hold it), each reached from T0 to T1; `gusts S [wind D] [speed V]`: fronts that
//     gusts    1.5  wind 90                                     # sweep across the grass and trees; `mist DENSITY [radius R]`: a mist round the camera
//     fog      0.04  top 30  thickness 4  lift 10 20             # fog lying in the valleys (height_fog.h): dense below the height `top` (pbrt's y; a quarter of the land's
//                                                               # height by default), thinning over `thickness`; lifting from 10 s to 20 s, it thins away and sinks
//     storm    rain  strength 0.8  from 4 to 40  wind 120       # a storm building up and dying away: the sky darkening under a deck of cloud, rain shafts, its rain
//                                                               # (snow: a blizzard; sand: a dust storm) blown by its wind (`speed V`), gusts, wet ground, lightning
//     air      strength 3  haze 1.5                             # the air between the camera and the far land (aerial perspective): the atmosphere's blue and haze,
//                                                               # thinning with height, out to `radius` (4.5 times the terrain's size); `sky ... physical [altitude A]`
//                                                               # makes the sky and the sun's light from the atmosphere itself (atmosphere.h), as the eye A m up sees it
//     underwater  clarity 1.5  colour teal                      # the camera is under the film's water (film_underwater.h): the sea round it, caustics on the seabed, shafts
//     kelp     60  in -20 -20 20 20  height 9                   # of light, marine snow; kelp swaying and coral (`coral COUNT [in X0 Z0 X1 Z1]`) on the seabed
//     geyser   at 30 -40  height 25  every 20                  # the land in action (film_geology.h): a geyser erupting every so often; `rockfall from X Z [at T]`: rocks
//                                                               # tumbling down the slope; `avalanche from X Z [at T]`: a powder cloud racing down; `quake T0 T1`: shaking
//     fire     at 0 0 -4  size 1.2                              # fire (film_fire.h): flames (glowing volumes, flickering, a light among them, sparks) with smoke above;
//     lava     from 120 -300  width 6                           # lava creeping down the land from a vent; a volcano raised in the land (`volcano at X Z [height H]
//     herd     deer 12 from 0 0 to 100 0  fur 1                # fur on a herd, `feathers A` on eagles and geese (film_fur.h): strands over the body
//     herd     zebras 30 from 0 0 to 80 0  coat zebra  hero 1  young 4  name grazers   # real animals (round 13): a coat's pattern (film_coats.h), real fur on
//                                                               # the first N (`hero`), young following their mothers; also rabbits, penguins, snakes
//     hunt     pride grazers at 6  stalk 3  catch               # a hunt between two named herds: the stalk, the charge, the prey scattering, a catch
//     flight   eagles 1  from 0 12 0  to 60 3 -20  at 2         # birds taking off from a perch, flying and landing on another (eagles or geese)
//     baitball 0 6 -10  count 500  strike 4  hunter sharks      # life under the sea (film_sealife.h): a bait ball split by sharks or dolphins;
//     reeffish 40  in -6 1 -14 6 4 4                            # reef fish in their colours; `jellyfish N in X0 Y0 Z0 X1 Y1 Z1 [glow G]`;
//                                                               # `seaturtles N at X Y Z [radius R]`
//     insects  bees 3  in -0.1 0.05 -0.1 0.1 0.12 0.1           # the small world (film_macro.h): bees, dragonflies (blurred wings), beetles
//     dew      400  in -0.25 -0.25 0.25 0.25                    # (iridescent: a thin film); dewy grass; use with a close `key` and a lens
//     night    eyeshine 1  flash 1                              # night (film_night.h): eyes shining back, a flash; `cameratrap [ir] [name W]
//     shimmer  0.004                                            # [clock HH:MM] [date D] [temp C]`: a trail camera's picture; heat shimmer
//     underwater  lamp 1  ripples                               # under the sea, round 14: a diver's lamp, rippled sand; looking up, Snell's window
//     waterline                                                 # (a shot's word: its camera floats at the surface, half above it, drops on its dome)
//     current  0.3 toward 90  surge 6                           # the water's flow (film_undersea.h): kelp, grass, snow, bubbles, fish all in it
//     bubbles  stream 2 0 -6  rate 8                            # bubbles rising (Boyle's law): `breath X Y Z [every P]`, `net X Z at T`, `ring X Y Z at T`
//     halocline -9                                              # a boundary between two waters: it shimmers, and mirrors at a glancing look
//     deepsea  lanternfish 60  in -4 2 -8 4 4 -3                # the deep (film_deepsea.h): anglerfish, lanternfish, combjellies, siphonophores; `deepsea sparks`
//     octopus  0 0 -3  toward 90  hide 6  jet 12  ink 11.8      # an octopus crawling, hiding, jetting off, inking; `cuttlefish N at X Y Z`
//     swimmers mantas 2  at 0 6 -12  radius 8                   # mantas, eaglerays, stingrays (`rise T`), sealions, penguins, otters
//     seabed   anemones 6  in -3 -6 3 -3                        # life on the seabed (film_seabed.h): anemones, seagrass, starfish, urchins, crabs, gardeneels
//     wreck    0 -20  toward 60  length 28  list 18             # places: a wreck, `arch X Z`, `cenote X Z [radius R] [depth D]`, `reefwall X Z [length L] [height H]`
//     diver    Ana from -6 3 -2 to 6 3 -10  lamp  ok 30          # round 15 (film_diver.h): a scuba diver on a path, breathing bubbles; `divecam` (a shot's word)
//     school   sardines 600 at 0 4 -12  strike 20                # a school that parts round the camera and bolts from a hunter (film_school.h)
//     bigfish  whaleshark 1 at 0 6 -20                           # whaleshark, hammerheads, reefsharks, dolphins, humpbacks
//     reef     60 in -8 -14 8 -4  fluoresce                      # a coral reef (film_reef.h); `critters nudibranchs|seahorses|shrimp|mantisshrimp N in ...`
//     nightdive glow 2  spawn 2 18                               # the water lighting up round what moves, corals spawning; `kelpforest N in X0 Z0 X1 Z1`
//     boat     0 -14  toward 90  speed 2  anchor 13              # round 16 (film_boat.h): a boat overhead, its wake, its anchor; `sardinerun X Y Z [gannets N]`
//     vents    3 in -6 -46 6 -40                                 # black smokers (film_vents.h); `murk X Y Z [radius R] [green|grey]`, `silt X Z at T`
//     tornado  150 at 0 3 -8  around Ana                         # barracuda (film_behaviour.h); `cleaningstation`, `playmate X Y Z with NAME`, `damselfish N at`
//     wreckroom 30 -20  length 12                                # inside a wreck; `underwater port flat filter red`: a camera's housing
//     character finn fish  at 0 -12 -4                           # round 17: the sea's characters (film_sea_kinds.h): fish, crab, octopus
//     clownfish 0 0 -6  count 3                                  # comic sea life (film_vignettes.h): `pufferfish X Y Z [at T]`, `flounder X Z [at T]`,
//                                                               # `moray X Y Z [at T]`, `cleanershrimp NAME`, `dugong X Z [radius R]`
//     seacave  0 -20  radius 8                                   # places: a sea cave, `ruins X Z [length L]` (a sunken city); `icecap [holes N]`: ice overhead
//     chest    0 -8  open 12                                     # treasure (film_treasure.h): `clam X Z [shut T]`, `bottle X Y Z`, `shells N in X0 Z0 X1 Z1`
//     sink     coin 0 3 -6  at 4                                 # something let go in the water: coin, anchor, rock, ball (it floats); `moonlight [elevation E]`
//     model    robot                                            # a model of the film's own (film_model.h), read up to `end`: pieces of shapes (add, blend K, cut,
//       piece  body colour 0.8 0.3 0.2                          #   intersect; `hard` for crisp edges), subdivision cages (`cage box W H D crease S`), modifiers
//       box    0 0.8 0 size 0.6 0.7 0.4 round 0.03              #   (bend, twist, taper, mirror, array, radial, lattice, solidify, smooth), `sweep`, `loft`,
//       mirror x                                                #   `paint`, `displace`, `kit`, and whole models by `make building|chair|table|car|rocket|boat`
//     end
//     place    robot  at 0 0 -3  turn 20                        # placed (or `scatter robot N in X0 Z0 X1 Z1`): instanced, drawn coarser far from the camera
//     place    robot  at 0 0 -3  as r1                          # round 2: named for `move r1`, `turn r1.arm.r ...`, `show r1.eyes ...`, `shatter r1 at T`
//     character bo robot  at 1 0 -2                             # a model as a character: rigged by its pieces' names (head, eyes, mouth, leg.N, arm.l/r, tail)
//     model    tall from post  h 3                              # parameters (`model post params h 1`, `$h`), `variants N h lo hi`, library/NAME.model; in a block:
//                                                               #   tool, boolean, budget, key, mesh FILE, texture, decal, displace ... bake, make tree|creature|street|room
//     flex     r1.neck stretch 0 1  2 1.5                       # round 3: shapes over time (flex, morph ID to MODEL, build, vanish, wobble), mechanisms (spin, gear,
//     spin     cart.wheel about 0 0.3 0 axis 1 0 0 rpm 60       #   piston, roll, route, aim, attach ID to NAME head), `rigidbody ID`; in a block: skinned, bone, fur,
//                                                               #   cloth, cover, stroke, remesh, close, region, unwrap, text "WORDS", pbr, finish skin
//     space    stars 1  milkyway 1  sun 60 15                   # the universe (film_space.h, round 10): a set in space - the stars all round, the Milky Way, the sun a
//                                                               # glowing ball at a bearing and an elevation; no sky, no air
//     planet   home ocean  at 0 0 -20000  radius 6000           # a planet (film_planets.h): rocky, desert, ice, ocean, gas or lava; `tilt`, `spin SECONDS`
//     nebula   veil  at 0 0 -30000  size 20000  kind pillars    # the universe beyond (film_cosmos.h): a glowing cloud of gas and dust (emission, dark, pillars)
//     galaxy   swirl  at 0 0 -80000  size 50000  tilt 60        # a galaxy: spiral, barred or elliptical, tipped by `tilt`, turned by `turn`
//     star     vega  at 0 0 -30000  radius 5000  kind blue      # a star close up: sun, red, orange, white, blue (or `temperature K`);
//                                                               # `prominences N` looping off its limb, `flare T` bursting from it
//     blackhole maw at 0 0 -30000  radius 2000  tilt 8  jets 20    # a black hole: its shadow, its disc (`disc` radii), the disc bent over it, `jets`
//     comet    halley at 0 0 -20000  move 300 0 0  tail 20000  # a comet: its nucleus, its coma, its ion and dust tails away from the sun (or `away X Y Z`)
//     asteroids belt at 0 0 -30000  radius 20000  count 4000      # rocks by the thousand: a `ring` going round (`width`, `tilt`) or a drifting `field`
//     remnant  crab at 0 0 -40000  size 12000  blast 3          # a dying star: a `supernova` (at `blast T`), a `pulsar` (`period`), a `planetary` nebula
//     ship     ark  at 0 0 -500  kind shuttle  size 60  move 0 0 -40   # a craft: shuttle, fighter, freighter, station, wheel; engines, running lights
//   (in a shot) spacecam flyby 0 0 -30000 radius 9000 from 20    # the camera's journey: orbit, flyby (a slingshot), zoom (powers of ten), hyperspace
//     space    flare 0.4  bloom 0.15  planetshine 2           # light in space: the lens's flare and glow, the planets' light on what is near them;
//                                                               # `hum H`: the hum of space under its sounds (engines, breathing, blasts...)
//     astronaut kim at 0 0 -6  tether ark  floaters 6  tumble 5    # someone floating in a spacesuit, tethered to a point or a ship, loose things round them
//     descent  pod from 0 20000 -60000 to 0 0 -2000 start 0 land 30   # a craft coming down: re-entry's fire, `chutes`, retro-rockets, dust
//     orbit    ark around home radius 9000 period 40 to 14000 at 12   # (round 11, film_orbits.h) a ship round a planet on Kepler's orbit (`apoapsis`,
//                                                               # `incline`, `phase`), a transfer to another (`to R at T`): burns, a flip to slow, thruster puffs
//     burn     ark 3 6                                         # its engines fire from 3 s to 6 s
//     laser    red1 at 2 to ark  count 4 every 0.3  colour green   # (film_battle.h) bolts from a ship's guns to a ship or a place (`to X Y Z`)
//     shield   ark  radius 40                                  # a bubble round a ship, seen only where a bolt strikes it: a ripple of light
//     explode  red2 at 6                                       # a ship blown up (gone from then on) or a blast at a place: flash, fireball, ring, wreck
//     gravity  1.62                                            # how strongly things fall (m/s^2): jumps hang, props fall slowly, a walk becomes a lope below 6
//     footprints ann                                           # the prints a character's feet leave (all of them: no names); dust kicked on clean arcs below 6
//     blob     drop at 0 1.4 -1  size 0.06  split 4            # (film_zerog.h) water floating with no weight, wobbling; `split T` into two; `on NAME`: a tear
//     drift    junk 20 at 0 1.2 0  box 3 2 3  kind mixed        # small things floating in a box - crumbs, pens, tools - tumbling, bouncing off its walls
//     hologram map globe at 0 1 2  size 0.6  colour 0.3 0.85 1   # (film_screens.h) a picture of light over a projector: globe, ship (`ship KIND`), orbits or map;
//                                                              #   scanlines, a slow turn (`spin`), `flicker`
//     screen tv at 0 1.5 3  show radar  size 1.6 0.9  facing 180   # (film_screens.h) bars, static, radar (the film's craft as blips: `centre`, `range`), trace,
//                                                              #   or `picture FILE`; `tilt`, `bright`
//     label "Voyager 1" on probe  from 2 to 9                    # (film_explain.h) an explainer's drawings: a card over a craft, planet, moon or character;
//     orbitline probe dashed  from 3                           #   the path a craft or moon will take; a scale bar; planets side by side at their true sizes
//     scalebar 1000 at 0 -50 400  text "1,000 km"
//     compare Earth Mars Moon at 0 0 300  height 40
//     interior cockpit at 0 0 0  facing 0                      # (film_interior.h) a corridor, a cockpit with a window onto space, an airlock (`cycle T`), a ring
//                                                               # station's floor curving up out of sight
//     closeup  size 16                                         # the ground close up (film_ground.h): ripples, drifts, pebbles, footprints, wet sand; `tracks from
//                                                               # X Z to X Z [during T0 T1] [kind boots|paws|hooves]`: a trail of footprints made over its time
//     caustics traced                                          # the net of light under water traced from the rippling surface (film_caustics.h);
//                                                               # `waterfall ... rainbow`: a rainbow in its spray when the sun is behind the camera
//     optics   halo 1  rays 0.6  alpenglow 1  greenflash          # the sky's optics (film_sky.h): a 22-degree halo and sun dogs, a fogbow (`fogbow S`),
//                                                               # crepuscular rays, rose light on the peaks round sunset, the green flash as the sun sets
//     wind     60 8                                            # the wind (film_wind.h): a bearing and m/s that the grass, trees, clouds, fog, rain, smoke and
//                                                               # waves all take; `tornado from X Z to X Z [during T0 T1] [width W]`: a funnel crossing the land
//     glacier  from 0 -400  toward 180  calve 20                # ice (film_ice.h): a glacier down the land, its front calving; `iceberg at X Z [size S]`,
//                                                               # `seaice [cover C] [floe F]`, `waterfall ... frozen`
//     fireflies 200  in -20 -20 20 20  together                # living light (film_glow.h): a swarm flashing (`together`: into step), `fungi COUNT in ...`
//                                                               # glowing on the ground, `bioluminescence [strength S]`: the stirred sea glowing blue
//     whale    at 0 -150  toward 90  breach 12                 # the sea's giants (film_giants.h): a humpback rolling, blowing, breaching; `dolphins 6 at X Z`
//                                                               # porpoising along `toward DEG` (both need a `water sea`)
//     wildfire in -200 -200 200 200  during 0 30  wind 90       # [radius R] [erupting T0 T1]`), and a fire front sweeping across with the wind; a volcano's `column H`
//                                                               # (an ash column with its umbrella), `lightning N` in it, `ashfall A`, `flow T [toward DEG]` (pyroclastic)
//     timelapse 60                                              # nature 60 times faster: the clouds, the water, the aurora, the wind (the camera and the characters not)
//     slowmo   22 30 0.3                                        # from 22 s to 30 s nature runs at 0.3 of its speed (0.25 when not given): a high-speed camera's slow motion
//     exposure auto                                             # (or a factor) the exposure follows the light: 1 by day, opening up through sunset into night
//     shot 0 Over the valley
//       fly  -800 200  0 -100  600 -600  height 80  look 0 40 -900   # (instead of `key` lines) a drone over waypoints at a height above the land, smoothed, looking ahead and
//                                                               # down (or at a point; `fov F`)
//
// Weather and particles (film_particles.h):
//
//     weather   rain 0.8 from 4 to 14                          # rain, snow or leaves over the set round the camera (amount 1 a good shower; eased in and out; `ground Y`)
//     particles sparks at 1 0.9 -2  3 5  rate 80               # sparks, dust or a splash thrown from a place from 3 s to 5 s, so many a second (`speed S`); its y is the
//                                                               # scene's own, not above a terrain
//     burst     splash at 0 0 -1  6.2  count 60                # all at once at 6.2 s
//
// Several sets in one film - a film in parts (partsOf; film_parts.h joins them): each `part` is a film of its own, with its `scene`, its `length`, its characters, its shots
// and its sound, and the parts play one after another. The lines before the first part are every part's (a cast, a default set, an fps), except the film's own `film`,
// `name`, `about`, `tags`, `still` (a time in the whole film) and `music`:
//
//     film     pip-goes-out
//     part     "The lantern path"                              # a part: its name ...
//     scene    film_assets/a-walk-at-dusk/set.pbrt
//     length   7
//     ...
//     part     "The meadow" fade 1.2                           # ... and how it comes in after the one before: a cut, or `dissolve`, `fade` or `wipe` over so many seconds
//     scene    film_assets/stage/set.pbrt
//
// The library - casts and sets kept once and brought into any film by name (library/NAME.cast and library/NAME.set beside the film file, else in film_assets/library):
//
//     set      meadow                                          # a set's lines read here: its scene (from the set's folder), its sky, dressing and sound
//     cast     ann  at -0.8 0 0  facing 30                     # a character as its cast file says (kind, look, voice), standing here
//     cast     kid as pip-child  at 0 0 0.8                    # ... named `kid` in this film; anything more on the line is on top of the cast's
//     rigged   bot models/bot.glb  height 1.6                  # a kind of character from a rigged glTF model with clips (film_rigged.h): idle, walk, run, and its gestures
//                                                              # (`stride S`, `turn DEGREES`, `walk CLIP`, `idle CLIP`, `run CLIP`); a cast file can name its model the same way
//                                                              # (`skinning dq|linear`; `autorig`: a skeleton fitted to it, auto_rig.h - an .obj or .ply gets one anyway)
//     character  bo person  pitch -2  rate 0.9                 # (a voice for any character: semitones up from its kind's, and how fast it speaks)
//
// Characters as a feature film draws them (round 12: film_parse_character.h, and `act` here):
//
//     skin-light [NAME] 1                                      # light goes into the skin (subsurface): ears and fingers glow red with a light behind them
//     eyes     [NAME] real                                     # a clear cornea over a fibred iris, pupils that open in the dark (eye_model.h)
//     hair     [NAME] long  [length L] [density D] [curl C] [frizz F] [clump C] [colour C] [width W]   # strands (film_hair.h): short bob long ponytail curly mop, off
//     footing  [NAME] ground                                   # each foot on the floor under it, tilted to it, the hips down to the lower one
//     cloth    skirt|coat|scarf on NAME [length L] [width W] [flare F] [colour C] [stiff S]   # clothes that hang from the body and swing round the legs (film_cloth.h)
//     act      NAME me.face at 2 [to T1] [amount A]            # a face acted from a video (face_track.h; tools/video_to_acting makes it, and the body's me.bvh)
//
// Real voices (film_tts.h) - lines said by the computer's own text-to-speech, offline, the mouth following the recording:
//
//     voices   system                                          # every character in one of the computer's voices (by its name; `babble` the default)
//     cast     ann  at -0.7 0 0  voice zira                    # ... or one of them in the voice with that word in its name
//     acting   words                                           # each line acted from its words (film_acting.h): a gesture and an expression where none is given
//     touch    ann 2 5  -0.3 1.2 -0.6 left                     # a hand on a place of the set from 2 s to 5 s (a wall to lean on, a door's handle), reached for and let go
//     hold     ann sam 6 9                                     # ann's right hand and sam's left held together
//     hug      pip kid 6 9                                     # both of each one's arms round the other (stand them close: about a third of a metre chest to chest)
//
// Titles and credits - a title card over the opening, cards of credits after the story over black, a theme under the title:
//
//     title    "The Lost Mitten"  at 0.5  for 3                # (the film's name unless given)
//     episode  2 "Winter"                                      # a line under the title: "Episode 2: Winter"
//     credits                                                  # cards of three seconds after the story: the name, the cast five to a card, the `credit` lines
//     credit   "Music by Sam"
//     theme    jingle                                          # a fanfare of its own under the title, or a WAV of the film library (`theme opening.wav gain 0.8`)
//
// A shot's own lighting (shot_lighting.h) - a key, a fill and a rim round its subject, from where the camera is (in a shot's lines; a cameras file can say it too):
//
//       lighting key 1.5 fill 0.5 rim 1.2 side right on ann    # (any of them; `lighting` alone: key 1.2, fill 0.4, rim 0.8 on what the camera looks at)
//
// The toon look (src/shared/toon_look.h) - the light in flat bands, ink lines round shapes and along creases, stronger colour - on every frame:
//
//     toon     bands 3  lines 2.5  ink #1a1420  saturation 1.25  shadow 0.35    # (any of them, or none for the default style; `toon off`)

#include "film_parse_nature.h"   // (and film_spec.h)
#include "film_parse_cartoon.h"   // (the cartoon lines: mark, cloth)

namespace cartoon {
namespace film_file {

// Reads a film file's text. `where` names it in the messages. The scene is not read here (see load()), so the names it animates are not yet checked. The library files it
// brings in (`set NAME`: library/NAME.set, film-file lines - a scene and what dresses it; `cast NAME`: library/NAME.cast, what a `character` line says of a character but
// where it stands) are read here, as if their lines were the film file's at that place.
// `castBefore`: a film in parts' last part is given the cast of the parts before it, for its credits.
Parsed parse(const std::string &text, const std::string &where, const std::vector<Spec::CreditName> *castBefore = nullptr);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A film in parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A film file may be cut into parts - `part NAME [dissolve|fade|wipe SECONDS]` - each a film of its own, with its set (`scene`), its length, its cast, its shots and its sound,
// played one after another as one film (film_parts.h). The lines before the first part are every part's (a cast, a set, an fps they share; a part's own `scene` or `length`
// is its own), except the film's own `film`, `name`, `about`, `tags`, `still` and `music`, which are the whole film's (`still` a time in the whole film).
struct PartText {
	std::string name;
	int line = 0;
	Transition in;      // how it comes in (length 0: a cut)
	std::string text;   // the file with the shared lines and this part's, the others left blank (so a problem names the file's own line)
};
struct FilmParts {
	std::vector<PartText> parts;   // none: the film is not in parts
	std::string key, name, about, tags, error;
	double still = -1.0;
	bool draftMusic = true;
};
FilmParts partsOf(const std::string &text, const std::string &where);

}  // namespace film_file
}  // namespace cartoon
