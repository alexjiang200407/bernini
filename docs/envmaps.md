# How to generate Environment Maps using CMFT Studio

## Import the source (.hdr file)
![alt text](./images/envmaps-1.png)

**Notes**
- Do not tonemap the skybox

## Generate the Radiance Texture

![alt text](./images/envmaps-2.png)

**Options**

- Edge Fixup -> Warp
- Disable "Use OpenCL" option
- Might need to modify Gamma
- Modify resolution depending on needs
- Set CPU cores depending on your Computer Specifications

**Click Process**

- Wait a while
- It may stall. If that is case Kill the process using task manager and restart

**Modify LOD**
- Modify LOD

**Click Save**

- File Type: dds
- Output Type: Cubemap
- Format RGBA32F, we compress later in Editor
- Then click **Save** again at the bottom

## Generate the Irradiance Texture

![alt text](./images/envmaps-3.png)

**Options**

- Might need to modify Gamma
- Modify resolution depending on needs. 128 is good enough

**Click Process**

- Wait a while

**Click Save**

- File Type: dds
- Output Type: Cubemap
- Format RGBA32F, we compress later in Editor
- Then click **Save** again at the bottom