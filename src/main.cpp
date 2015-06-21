
#include "snesgss.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

// ::TODO license source copyright::

void exportGsm(const std::string filename, const std::string dirname)
{
	bool s;
	int r;

	SnesGssExporter *exporter = new SnesGssExporter();

	std::cout << "Loading " << filename << "\n";
	s = exporter->ModuleOpen(filename);

	if (s) {
		std::cout << "Cleaning up songs: ";
		exporter->CleanupSongs();
		std::cout << "DONE\n";

		std::cout << "Cleaning up instruments: ";
		r = exporter->CleanupInstruments();
		std::cout << r << " Instruments removed\n";

		std::cout << "Exporting: ";
		exporter->Export(dirname);
		std::cout << "DONE\n";
	}
}


int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "USAGE: " << argv[0] << " <gsm> <export dir>\n";
		return EXIT_FAILURE;
	}
	else {
		exportGsm(argv[1], argv[2]);
		return EXIT_SUCCESS;
	}
}

