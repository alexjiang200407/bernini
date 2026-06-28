import IGeom;

public
interface IMeshInstance
{
	associatedtype GeomType : IGeom;

public
	uint GetBaseMeshID();
};
