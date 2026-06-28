

public
struct Entry<T>
{
	uint offset = 0xFFFFFFFF;

public
	uint
	GetOffset()
	{
		return offset;
	}

public
	bool
	Null()
	{
		return offset == 0xFFFFFFFF;
	}
};
