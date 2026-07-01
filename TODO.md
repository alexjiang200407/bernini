
- Asset loading
- Phong-Blinn Deferred Shading
- Frame Graph optimization
    - Wasted barriers. Add a PreTransition function to PassDesc to determine if a Barrier is necessary
    - We can merge barriers instead of completely changing the Barrier.
- Motion Vectors