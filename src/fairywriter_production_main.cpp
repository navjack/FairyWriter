#include <QApplication>
#include <cstdio>
#include <cstring>

int fairywriter_run_embedded_snes(int argc, char** argv);

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
			std::fputs("FairyWriter " VERSIONSTR "\n", stdout);
			return 0;
		}
	}
	return fairywriter_run_embedded_snes(argc, argv);
}
