class CBaseDC
{
public:
	uint8_t pad[0x8];					// 00-08
	void* operator new(size_t nSize, uint32_t nUnk0)
	{
		return ((CBaseDC * (__cdecl*)(size_t, uint32_t))(AddressSetter::Get("CBaseDC", "new")))(nSize, nUnk0);
	}
	void operator delete(void*, size_t, uint32_t) {};
	void Add()
	{
#ifdef IVSDK_CE
		// CE: the draw command is passed on the stack (__cdecl) instead of in ECX
		((void(__cdecl*)(CBaseDC*))(AddressSetter::Get("CBaseDC", "Add")))(this);
#else
		((void(__thiscall*)(CBaseDC*))(AddressSetter::Get("CBaseDC", "Add")))(this);
#endif
	}
};
VALIDATE_SIZE(CBaseDC, 0x8);