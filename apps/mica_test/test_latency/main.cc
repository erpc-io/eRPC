#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "mica/util/latency.h"

int main(int argc, char **argv)
{
	::mica::util::Latency lat;

	for(unsigned l = 0; l < 200; l++) {
		lat.update(l);
	}

	printf("Average latency = %u\n", (unsigned) lat.avg());
	printf("50th percentile  = %u\n", (unsigned) lat.perc(.5));
	printf("99th percentile = %u\n", (unsigned) lat.perc(.99));
}
