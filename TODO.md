
- Asset loading

- idlgen support constants

- Add Submesh structure
    - Per-part vertex layout.
    - Per-part bounds. Remove bounding box from meshlet
    - References a material, Entry<T> templated here defaults to IMaterial
    - References a range of Meshlets
    - Many to one relationship with Mesh

- Show the new Mesh schema


- Phong-Blinn Deferred Shading
- Frame Graph optimization
    - Wasted barriers. Add a PreTransition function to PassDesc to determine if a Barrier is necessary
    - We can merge barriers instead of completely changing the Barrier.
- Motion Vectors