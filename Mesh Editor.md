# Import Mesh

- DO NOT IMPORT MATERIALS OR TEXTURES ANYMORE GLB IMPORT GEOM ONLY


## Import Window
- Currently we select the texture output directory but we just want to dump to ./textures_src



## Material Editor

- Inside Material Directory, right click to add material
- Go to material editor
- material editor design:
  - Left panel blackboard
  - Right panel viewer
  - Drag and drop mesh to right panel for selected material
  - If no mesh selected used default sphere
  - output node has 3 outputs basecolor, orm, normal. orm output can be expanded to ao roughness and metallic

- Drag and drop textures either from disk or from in-editor (this will create a converted (non-compressed) ktx2 texture to texture_raw directory) which appear as nodes
- The texture will have a r,g,b,a output depending on if it has alpha or not
- Then drag and drop the channels into the output color nodes
- Select resolution
- Click generate


- Open a window, user inputs material output directory, texture output directory, must be prefixed correctly with textures/ or materials/ respectively
- Will generate 3 textures: basecolor (alpha unused), orm, normal map (alpha unused)


## QOL



- Inside Mesh Directory, right click to add mesh
- Open file dialog
- Select then go to the usual asset import window
