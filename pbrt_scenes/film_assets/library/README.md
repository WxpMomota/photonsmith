The film library: casts and sets that any film file can bring in by name (read by src/shared/cartoon/film_parse.cpp).

  cast NAME [as WHO] [at X Y Z] [facing DEGREES] ...   a character: WHO.cast (WHO is NAME unless given) - its kind, look and voice - standing where this line says
  set  NAME                                            a set: NAME.set - a scene and what dresses it, its sky and its sound - as if its lines were the film's

A film looks for library/NAME.cast and library/NAME.set beside its own file first, then here (film_assets/library where the scenes are), so a film can keep its own
characters next to it and still use the shared ones.

A .cast file is what a `character` line says of a character but where it stands: a `kind` line, then one line for each other thing (`scale 0.8`, `sweater red`,
`pitch -3`, `rate 0.95` ...); a `rigged ...` line makes its kind from a rigged model, the file from the cast's folder. A .set file is film-file lines: `scene FILE.pbrt`
(from the set's own folder), `sky`, `place`, `scatter`, `ambience`, `terrain`, `forest`, `water`, `weather` and so on - and even characters, and other sets (at most four
deep) - but not the film's own lines (film, name, about, tags, still, length, fps, music, shot, key, part, title, episode, credits, credit, theme: each is an error there).
A problem in a set's lines is reported with the set file's name and line, not the film's; a cast without a `kind` line names the cast file, and a bad word of a
cast's look is reported at the `cast` line that brought it in.

Here: casts pip, pip-child, kai, ann, sam, shelly and robot; sets dusk-path, meadow, valley and void (an empty stage for a film in space, void.pbrt).

animals/ holds the films' rigged animals (glTF models with their clips: deer, horse, wolf, eagle, goose, bear, elephant, fox, giraffe, lion, penguin, rabbit, snake, zebra;
made by scripts/make_rigged_animals.py), which `herd` and `flock` lines bring in (src/shared/cartoon/film_animals.h). robot.glb is a rigged character a `rigged` line can
make a kind of (scripts/make_rigged_robot.py; robot.cast names it), and mira.glb one with a face of morph targets (scripts/make_rigged_face.py). stars/ is the star
catalogue and constellation lines of the real night sky (d3-celestial; its licence is beside them).
