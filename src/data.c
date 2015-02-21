#include "bolo.h"

int sample_data(sample_t *s, double v)
{
	if (s->n == 0) {
		s->min = s->max = v;
	} else {
		if (v < s->min) s->min = v;
		if (v > s->max) s->max = v;
	}
	s->sum += v;
	s->n++;

	s->mean_ = s->mean;
	s->mean  = s->mean_ + (v - s->mean_) / s->n;

	s->var_ = s->var;
	s->var = ( (s->n - 1) * s->var_ + ( (v - s->mean_) * (v - s->mean) ) ) / s->n;

	return 0;
}
