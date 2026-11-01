"""Tools for Ballance levels on top of libnmo's nmo CLI.

The package is layered; each layer uses only those above it in this list:

- core: what has nothing to do with Ballance -- headings and points on the
  XZ plane (geometry), OBJ meshes (obj), and the nmo CLI (nmo);
- game: what the original game holds -- its groups, element kinds and
  mechanisms (catalog), the dimensions measured on its levels (measures), its
  materials (materials);
- kit: the geometry the original levels are built from -- floors on their
  grid (floors), the pieces off it (pieces), rails (rails);
- route: a route laid out as a level -- the track and its pieces (layout,
  pieces), the modules (modules), the scenery (scenery), the meshes they use
  (assets), the manifest (manifest), and writing it all (build);
- extract: what is read out of the game -- element meshes (elements), the
  materials (materials), and checks of a level file (check);
- view: drawing a level -- its objects (scene), the rasteriser (render),
  routes and ball paths over it (overlays), the image (sheet).

The command line tools in apps/ballance/scripts are thin: they parse their
arguments and call into here.
"""
