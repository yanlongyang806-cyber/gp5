#include "gimmeDLLWrapper.h"
#include "sock.h"
#include "utils.h"
#include "utilitiesLib.h"

int main()
{
	char line[256];

	EXCEPTION_HANDLER_BEGIN

	DO_AUTO_RUNS
	setDefaultAssertMode();
	gimmeDLLDisable(1);
	utilitiesLibStartup();

	while (fgets(line, sizeof(line), stdin))
	{
		U32 ip;
		size_t len = strlen(line);
		if (len && line[len - 1] == '\n')
			line[len - 1] = 0;
		ip = ipFromNumericString(line);
		printf("%s %s\n", line, ipToCountryName(ip));
	}

	EXCEPTION_HANDLER_END

	return 0;
}
