#include <process.h>
#include <time.h>
#include <windows.h>
#include <Wincrypt.h>

#include "AppRegCache.h"
#include "utils.h"

#pragma comment(lib, "crypt32.lib")

// Convert Raw Bytes To/From Hexadecimal String
const static char sgHexTable[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static char decodeHexCharToValue (const char encoded) {
	// Both uppercase and lowercase are OK
	if ('A' <= encoded && encoded <= 'Z')
		return (10 + (encoded - 'A'));
	if ('a' <= encoded && encoded <= 'z')
		return (10 + (encoded - 'a'));
	if ('0' <= encoded && encoded <= '9')
		return (encoded - '0');
	return -1;
}

// Hex string must contain an even number of characters to match byte offseting
int decodeHexString(const unsigned char *encoded, size_t src_size, char *buffer, size_t buffer_size)
{
	size_t i;
	int count = 0;

	if (buffer_size < src_size / 2) // round up, NULL terminator not necessary (since buffer is random bytes)
	{
		return -1;
	}

	if (buffer_size == src_size / 2 && src_size % 2)
	{
		return -1;
	}

	for (i=0; i<src_size; i++)
	{
		int val = decodeHexCharToValue(encoded[i]);
		if (val < 0)
			return -1;

		if (i % 2 == 0)
		{
			buffer[count] = val << 4;
		}
		else
		{
			buffer[count++] |= val & 0x0F;
		}
	}
	return count;
}

int encodeHexString(const unsigned char *unencoded, size_t src_size, char *buffer, size_t buffer_size)
{
	size_t i;
	int count = 0;

	if (buffer_size < src_size * 2 + 1) // include NULL terminator
	{
		return -1;
	}

	for (i=0; i<src_size; i++)
	{
		int index = (unencoded[i] >> 4) & 0x0F;
		buffer[count++] = sgHexTable[index];
		index = unencoded[i] & 0x0F;
		buffer[count++] = sgHexTable[index];
	}
	buffer[count] = 0;
	return count;
}

// Reimplemented for CrypticTorrent.
static void fallbackGenerateMachineID(char *buffer, size_t size)
{
	int count;
	int i;
	char random[32];

	// Seed RNG.
	srand((unsigned)time(0) * _getpid());

	// Fill random buffer.
	for (i = 0; i != sizeof(random); ++i)
		random[i] = rand() & 0xff;

	// Convert to hexadecimal.
	count = encodeHexString(random, sizeof(random), buffer, size);
}

// Reimplemented for CrypticTorrent.
static int generateMachineID(char *buffer, size_t size)
{
	HCRYPTPROV hCryptProv;
	char random[32];
	BOOL success;
	int count;

	// Acquire context.
	success = CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, 0);
	if (!success)
		return 0;

	// Generate random buffer.
	success = CryptGenRandom(hCryptProv, sizeof(random), random);
	if (!success)
	{
		CryptReleaseContext(hCryptProv, 0);
	}

	// Release context.
	CryptReleaseContext(hCryptProv, 0);

	// Convert to hexadecimal.
	count = encodeHexString(random, sizeof(random), buffer, size);

	return count > 1;
}

const char *getMachineID(char *raw32)
{
	static char machineID[MACHINE_ID_MAX_LEN] = "";
	static char machineIDraw[32] = "???";

	if (!machineID[0])
	{
		regGetAppString_ForceAppName("Core", "machineid", "", machineID, sizeof(machineID));
		if (!machineID[0])
		{
			int success = generateMachineID(machineID, sizeof(machineID));
			if (!success)
				fallbackGenerateMachineID(machineID, sizeof(machineID));
			regPutAppString_ForceAppName("Core", "machineid", machineID);
		}
		decodeHexString(machineID, strlen(machineID), machineIDraw, sizeof(machineIDraw));
	}

	if (raw32)
		memcpy(raw32, machineIDraw, sizeof(machineIDraw));

	return machineID;
}
