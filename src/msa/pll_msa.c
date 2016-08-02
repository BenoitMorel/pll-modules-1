/*
 Copyright (C) 2016 Diego Darriba

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 Contact: Diego Darriba <Diego.Darriba@h-its.org>,
 Exelixis Lab, Heidelberg Instutute for Theoretical Studies
 Schloss-Wolfsbrunnenweg 35, D-69118 Heidelberg, Germany
 */
#include "pll_msa.h"

PLL_EXPORT double * pll_msa_empirical_frequencies(pll_partition_t * partition)
{
  unsigned int i, j, k, n;
  unsigned int states         = partition->states;
  unsigned int sites          = partition->sites;
  unsigned int rate_cats      = partition->rate_cats;
  unsigned int tips           = partition->tips;
  const unsigned int * tipmap = partition->tipmap;
  const unsigned int * w      = partition->pattern_weights;
  double * frequencies;

  if ((frequencies = (double *) calloc ((size_t) states, sizeof(double)))
      == NULL)
  {
    pll_errno = PLL_ERROR_MEM_ALLOC;
    snprintf (pll_errmsg, 200, "Cannot allocate memory for empirical frequencies");
    return NULL;
  }

  if (partition->attributes & PLL_ATTRIB_PATTERN_TIP)
  {
    if (states == 4)
    {
      for (i = 0; i < tips; ++i)
            {
              const unsigned char *tipchars = partition->tipchars[i];
              for (n = 0; n < sites; ++n)
              {
                unsigned int state = (unsigned int) tipchars[n];
                double sum_site = 1.0 * __builtin_popcount(state);
                for (k = 0; k < states; ++k)
                {
                  if (state & 1)
                    frequencies[k] += w[n] / sum_site;
                  state >>= 1;
                }
              }
            }
    }
    else
    {
      for (i = 0; i < tips; ++i)
      {
        const unsigned char *tipchars = partition->tipchars[i];
        for (n = 0; n < sites; ++n)
        {
          unsigned int state = tipmap[(int) tipchars[n]];
          double sum_site = 1.0 * __builtin_popcount(state);
          for (k = 0; k < states; ++k)
          {
            if (state & 1)
              frequencies[k] += w[n] / sum_site;
            state >>= 1;
          }
        }
      }
    }
  }
  else
  {
    for (i = 0; i < tips; ++i)
    {
      for (n = 0, j = 0; j < sites * states * rate_cats; j += (states * rate_cats), ++n)
      {
        double sum_site = 0.0;
        for (k = 0; k < states; ++k)
          sum_site += partition->clv[i][j + k];
        for (k = 0; k < states; ++k)
          frequencies[k] += w[n] * partition->clv[i][j + k] / sum_site;
      }
    }
  }

  /* IMPORTANT: we must use the original number of sites in alignment (before pattern compression),
   * since we previously multiplied our base counts with the respective column weights! */
  unsigned int uncomp_sites = 0;
  for (i = 0; i < sites; ++i)
    uncomp_sites += w[i];

  for (k = 0; k < states; ++k)
    frequencies[k] /= uncomp_sites * tips;

#ifndef NDEBUG
  double sum_test = 0.0;
  for (k = 0; k < states; ++k)
  {
    sum_test += frequencies[k];
  }
  assert(fabs (sum_test - 1) < 1e-6);
#endif

  return frequencies;
}

PLL_EXPORT double * pll_msa_empirical_subst_rates(pll_partition_t * partition)
{
  unsigned int i, j, k, n;
  unsigned int states              = partition->states;
  unsigned int sites               = partition->sites;
  unsigned int tips                = partition->tips;
  unsigned int rate_cats           = partition->rate_cats;
  const unsigned int * tipmap      = partition->tipmap;
  const unsigned int * w           = partition->pattern_weights;
  unsigned char * const * tipchars = partition->tipchars;

  unsigned int n_subst_rates  = (states * (states - 1) / 2);
  double * subst_rates = (double *) calloc ((size_t) n_subst_rates, sizeof(double));

  unsigned *pair_rates = (unsigned *) calloc(
      states * states, sizeof(unsigned));
  unsigned *state_freq = (unsigned *) malloc(states * sizeof(unsigned));

  if (!(subst_rates && pair_rates && state_freq))
  {
    pll_errno = PLL_ERROR_MEM_ALLOC;
    snprintf (pll_errmsg, 200,
              "Cannot allocate memory for empirical subst rates");
    if (subst_rates)
      free (subst_rates);
    if (pair_rates)
      free (pair_rates);
    if (state_freq)
      free (state_freq);
    return NULL;
  }

  unsigned int undef_state = (unsigned int) (pow (2, states)) - 1;
  if (partition->attributes & PLL_ATTRIB_PATTERN_TIP)
  {
    if (states == 4)
    {
      for (n = 0; n < sites; ++n)
      {
        memset (state_freq, 0, sizeof(unsigned) * (states));
        for (i = 0; i < tips; ++i)
        {
          unsigned int state = (unsigned int) tipchars[i][n];
          if (state == undef_state)
            continue;
          for (k = 0; k < states; ++k)
          {
            if (state & 1)
              state_freq[k]++;
            state >>= 1;
          }
        }

        for (i = 0; i < states; i++)
        {
          if (state_freq[i] == 0)
            continue;
          for (j = i + 1; j < states; j++)
          {
            pair_rates[i * states + j] += state_freq[i] * state_freq[j] * w[n];
          }
        }
      }
    }
    else
    {
      for (n = 0; n < sites; ++n)
      {
        memset (state_freq, 0, sizeof(unsigned) * (states));
        for (i = 0; i < tips; ++i)
        {
          unsigned int state = tipmap[(int) tipchars[i][n]];
          if (state == undef_state)
            continue;
          for (k = 0; k < states; ++k)
          {
            if (state & 1)
              state_freq[k]++;
            state >>= 1;
          }
        }

        for (i = 0; i < states; i++)
        {
          if (state_freq[i] == 0)
            continue;
          for (j = i + 1; j < states; j++)
          {
            pair_rates[i * states + j] += state_freq[i] * state_freq[j] * w[n];
          }
        }
      }
    }
  }
  else
  {
    unsigned int cur_site = 0;
    for (n = 0; n < sites * states * rate_cats; n += (states * rate_cats))
    {
      memset (state_freq, 0, sizeof(unsigned) * (states));
      for (i = 0; i < tips; ++i)
      {
        int unstate = 1;
        for (k = 0; k < states; ++k)
          if (partition->clv[i][n + k] < 1e-7)
          {
            unstate = 0;
            break;
          }
        if (unstate)
          continue;
        for (k = 0; k < states; ++k)
        {
          if (partition->clv[i][n + k] > 0)
          {
            state_freq[k]++;
          }
        }
      }

      for (i = 0; i < states; i++)
      {
        if (state_freq[i] == 0)
          continue;
        for (j = i + 1; j < states; j++)
        {
          pair_rates[i * states + j] += state_freq[i] * state_freq[j]
              * w[cur_site];
        }
      }
      cur_site++;
    }
  }

  k = 0;
  double last_rate = pair_rates[(states - 2) * states + states - 1];
  if (last_rate < 1e-7)
    last_rate = 1;
  for (i = 0; i < states - 1; i++)
  {
    for (j = i + 1; j < states; j++)
    {
      subst_rates[k++] = pair_rates[i * states + j] / last_rate;
      if (subst_rates[k - 1] < 0.01)
        subst_rates[k - 1] = 0.01;
      if (subst_rates[k - 1] > 50.0)
        subst_rates[k - 1] = 50.0;
    }
  }
  subst_rates[k - 1] = 1.0;

  free (state_freq);
  free (pair_rates);
  
  return subst_rates;
}

PLL_EXPORT double pll_msa_empirical_invariant_sites(pll_partition_t *partition)
{
  unsigned int n;
  unsigned int n_inv      = 0;
  unsigned int sites      = partition->sites;
  const unsigned int * w  = partition->pattern_weights;

  /* reset errno */
  pll_errno = 0;

  if (!partition->invariant)
    if (!pll_update_invariant_sites(partition))
      return (double) -INFINITY;

  const int * invariant = partition->invariant;

  unsigned int uncomp_sites = 0;
  for (n=0; n<sites; ++n)
    {
      if (invariant[n] > -1)
        n_inv += w[n];
      uncomp_sites += w[n];
    }

  double empirical_pinv = (double)1.0*n_inv/uncomp_sites;
  return empirical_pinv;
}
