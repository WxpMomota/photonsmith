The film library: casts and sets that any film file can bring in by name (src/shared/cartoon/film_file.h).

  cast NAME [as WHO] [at X Y Z] [facing DEGREES] ...   a character: WHO.cast (WHO is NAME unless given) - its kind, look and voice - standing where this line says
  set  NAME                                            a set: NAME.set - a scene and what dresses it, its sky and its sound - as if its lines were the film's

A film looks for library/NAME.cast and library/NAME.set beside its own file first, then here (film_assets/library where the scenes are), so a film can keep its own
characters next to it and still use the shared ones.

A .cast file is what a `character` line says of a character but where it stands: a `kind` line, then one line for each other thing (`scale 0.8`, `sweater red`,
`pitch -3`, `rate 0.95` ...). A .set file is film-file lines: `scene FILE.pbrt` (from the set's own folder), `sky`, `place`, `scatter`, `ambience`, `terrain`,
`forest`, `water`, `weather` and so on - and even characters - but not the film's own lines (film, name, length, fps, shots, parts).
