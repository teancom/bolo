#include "bolo.h"

pdu_t *vx_pdu_dup(pdu_t *from, const char *type)
{
	pdu_t *to = pdu_make(type ? type : pdu_type(from), 0);
	if (vx_pdu_copy(to, from, 1, 0) != 0) {
		pdu_free(to);
		return NULL;
	}
	return to;
}

int vx_pdu_copy(pdu_t *to, pdu_t *from, int start, int n)
{
	int i, end = n ? start + n : pdu_size(from);
	for (i = start; i < pdu_size(from) && i < end; i++) {
		size_t len;
		uint8_t *buf = pdu_segment(from, i, &len);
		pdu_extend(to, buf, len);
		free(buf);
	}
	return 0;
}
