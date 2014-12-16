#include "wire.h"

result_t* nsca_result(uint8_t *buf, size_t len)
{
	return NULL;
}

result_t* bolo_result(uint8_t *buf, size_t len)
{
	return NULL;
}

void result_free(result_t *r)
{
	free(r);
}
