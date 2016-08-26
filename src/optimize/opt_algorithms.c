/*
 Copyright (C) 2015 Diego Darriba

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
#include "pll_optimize.h"
#include "lbfgsb/lbfgsb.h"
#include "../pllmod_common.h"

static inline int is_nan(double v)
{
  return v!=v;
}

static inline int d_equals(double a, double b)
{
  return (fabs(a-b) < 1e-10);
}

/******************************************************************************/
/* NEWTON-RAPHSON OPTIMIZATION */
/******************************************************************************/

PLL_EXPORT double pllmod_opt_minimize_newton(double x1,
                                            double xguess,
                                            double x2,
                                            double tolerance,
                                            unsigned int max_iters,
                                            void * params,
                                            void (deriv_func)(void *,
                                                              double,
                                                              double *, double *))
{
  unsigned int i;
  double df, dx, dxold, f;
  double temp, xh, xl, rts, rts_old;

  /* reset errno */
  pll_errno = 0;

  rts = xguess;
  if (rts < x1)
    rts = x1;
  if (rts > x2)
    rts = x2;

  deriv_func((void *)params, rts, &f, &df);

  DBG("[NR deriv] BL=%f   f=%f  df=%f  nextBL=%f\n", rts, f, df, rts-f/df);
  if (!isfinite(f) || !isfinite(df))
  {
    pllmod_set_error(PLLMOD_OPT_ERROR_NEWTON_DERIV,
                     "wrong likelihood derivatives");
    return (double) -INFINITY;
  }
  if (df >= 0.0 && fabs (f) < tolerance)
    return rts;
  if (f < 0.0)
  {
    xl = rts;
    xh = x2;
  }
  else
  {
    xh = rts;
    xl = x1;
  }

  dx = dxold = fabs (xh - xl);
  for (i = 1; i <= max_iters; i++)
  {
    rts_old = rts;
    if ((df <= 0.0) // function is concave
    || (((rts - xh) * df - f) * ((rts - xl) * df - f) >= 0.0) // out of bound
        )
    {
      dxold = dx;
      dx = 0.5 * (xh - xl);
      rts = xl + dx;
      if (xl == rts)
        return rts;
    }
    else
    {
      dxold = dx;
      dx = f / df;
      temp = rts;
      rts -= dx;
      if (temp == rts)
        return rts;
    }
    if (fabs (dx) < tolerance || (i == max_iters))
      return rts_old;

    if (rts < x1) rts = x1;

    deriv_func((void *)params, rts, &f, &df);

    if (!isfinite(f) || !isfinite(df))
    {
      pllmod_set_error(PLLMOD_OPT_ERROR_NEWTON_DERIV,
                       "Wrong likelihood derivatives [it]");
      return (double) -INFINITY;
    }

    if (df > 0.0 && fabs (f) < tolerance)
    {
      return rts;
    }

    if (f < 0.0)
      xl = rts;
    else
      xh = rts;
  }

  pllmod_set_error(PLLMOD_OPT_ERROR_NEWTON_LIMIT,
                   "Exceeded maximum number of iterations");
  return (double) -INFINITY;
}

/******************************************************************************/
/* L-BFGS-B OPTIMIZATION */
/******************************************************************************/

PLL_EXPORT double pllmod_opt_minimize_lbfgsb (double * x,
                                             double * xmin,
                                             double * xmax,
                                             int * bound,
                                             unsigned int n,
                                             double factr,
                                             double pgtol,
                                             void * params,
                                             double (*target_funk)(
                                                     void *,
                                                     double *))
{
  unsigned int i;

  /* L-BFGS-B parameters */
  //  double initial_score;
  int max_corrections;
  double score = 0;
  double *g, *wa;
  int *iwa;

  int taskValue;
  int *task = &taskValue;

  int csaveValue;
  int *csave = &csaveValue;
  double dsave[29];
  int isave[44];
  logical lsave[4];

  int iprint = -1;

  max_corrections = 5;

  /* reset errno */
  pll_errno = 0;

  g = (double *) calloc ((size_t) n, sizeof(double));

  /*     We start the iteration by initializing task. */
  *task = (int) START;

  iwa = (int *) calloc (3 * (size_t) n, sizeof(int));
  wa = (double *) calloc (
      (2 * (size_t)max_corrections + 5) * (size_t)n
          + 12 * (size_t)max_corrections * ((size_t)max_corrections + 1),
      sizeof(double));

  if (!(wa && iwa && g))
  {
    pllmod_set_error(PLL_ERROR_MEM_ALLOC,
                     "Cannot allocate memory for l-bfgs-b variables");
    if (g)
      free (g);
    if (iwa)
      free (iwa);
    if (wa)
      free (wa);
    return (double) -INFINITY;
  }

//  double initial_score = target_funk (params, x);
  int continue_opt = 1;
  while (continue_opt)
  {
    /*     This is the call to the L-BFGS-B code. */
    setulb ((int *)&n, &max_corrections, x, xmin, xmax,
            bound, &score, g, &factr, &pgtol, wa, iwa,
            task, &iprint, csave, lsave, isave, dsave);
    if (IS_FG(*task))
    {
      /*
       * the minimization routine has returned to request the
       * function f and gradient g values at the current x.
       * Compute function value f for the sample problem.
       */

      score = target_funk(params, x);

      if (is_nan(score) || d_equals(score, (double) -INFINITY))
        break;

      double h, temp;
      for (i = 0; i < n; i++)
      {
        temp = x[i];
        h = PLL_LBFGSB_ERROR * fabs (temp);
        if (h < 1e-12)
          h = PLL_LBFGSB_ERROR;

        x[i] = temp + h;
        h = x[i] - temp;
        double lnderiv = target_funk(params, x);

        g[i] = (lnderiv - score) / h;

        /* reset variable */
        x[i] = temp;
      }
    }
    else if (*task != NEW_X)
      continue_opt = 0;
  }

  /* fix optimal parameters */
  score = target_funk(params, x);

  free (iwa);
  free (wa);
  free (g);

  if (is_nan(score))
  {
    score = (double) -INFINITY;
    /* set errno only if it was not set by some inner function */
    if (!pll_errno)
    {
      pllmod_set_error(PLLMOD_OPT_ERROR_LBFGSB_UNKNOWN, "Unknown LBFGSB error");
    }
  }

  return score;
} /* pllmod_opt_minimize_lbfgsb */

/******************************************************************************/
/* BRENT'S OPTIMIZATION */
/******************************************************************************/

#define ITMAX 100
#define CGOLD 0.3819660
#define ZEPS 1.0e-7
#define SHFT(a,b,c,d) (a)=(b);(b)=(c);(c)=(d);
#define SIGN(a,b) ((b) >= 0.0 ? fabs(a) : -fabs(a))

static double brent_opt (double ax, double bx, double cx, double tol,
                         double *foptx, double *f2optx, double fax,
                         double fbx, double fcx,
                         void * params,
                         double (*target_funk)(
                             void *,
                             double))
{
  int iter;
  double a, b, d = 0, etemp, fu, fv, fw, fx, p, q, r, tol1, tol2, u, v, w, x,
      xm;
  double xw, wv, vx;
  double e = 0.0;

  a = (ax < cx ? ax : cx);
  b = (ax > cx ? ax : cx);
  x = bx;
  fx = fbx;
  if (fax < fcx)
  {
    w = ax;
    fw = fax;
    v = cx;
    fv = fcx;
  }
  else
  {
    w = cx;
    fw = fcx;
    v = ax;
    fv = fax;
  }

  for (iter = 1; iter <= ITMAX; iter++)
  {
    xm = 0.5 * (a + b);
    tol2 = 2.0 * (tol1 = tol * fabs (x) + ZEPS);
    if (fabs (x - xm) <= (tol2 - 0.5 * (b - a)))
    {
      *foptx = fx;
      xw = x - w;
      wv = w - v;
      vx = v - x;
      *f2optx = 2.0 * (fv * xw + fx * wv + fw * vx)
          / (v * v * xw + x * x * wv + w * w * vx);
      return x;
    }

    if (fabs (e) > tol1)
    {
      r = (x - w) * (fx - fv);
      q = (x - v) * (fx - fw);
      p = (x - v) * q - (x - w) * r;
      q = 2.0 * (q - r);
      if (q > 0.0)
        p = -p;
      q = fabs (q);
      etemp = e;
      e = d;
      if (fabs (p) >= fabs (0.5 * q * etemp) || p <= q * (a - x)
          || p >= q * (b - x))
        d = CGOLD * (e = (x >= xm ? a - x : b - x));
      else
      {
        d = p / q;
        u = x + d;
        if (u - a < tol2 || b - u < tol2)
          d = SIGN(tol1, xm - x);
      }
    }
    else
    {
      d = CGOLD * (e = (x >= xm ? a - x : b - x));
    }

    u = (fabs (d) >= tol1 ? x + d : x + SIGN(tol1, d));
    fu = target_funk (params, u);
    if (fu <= fx)
    {
      if (u >= x)
        a = x;
      else
        b = x;

      SHFT(v, w, x, u)
      SHFT(fv, fw, fx, fu)
    }
    else
    {
      if (u < x)
        a = u;
      else
        b = u;
      if (fu <= fw || w == x)
      {
        v = w;
        w = u;
        fv = fw;
        fw = fu;
      }
      else if (fu <= fv || v == x || v == w)
      {
        v = u;
        fv = fu;
      }
    }
  }

  *foptx = fx;
  xw = x - w;
  wv = w - v;
  vx = v - x;
  *f2optx = 2.0 * (fv * xw + fx * wv + fw * vx)
      / (v * v * xw + x * x * wv + w * w * vx);

  return x;
}

#undef ITMAX
#undef CGOLD
#undef ZEPS
#undef SHFT
#undef SIGN

/* most of the code for Brent optimization taken from IQ-Tree
 * http://www.cibiv.at/software/iqtree
 * --------------------------------------------------------------------------
 * Bui Quang Minh, Minh Anh Thi Nguyen, and Arndt von Haeseler (2013)
 * Ultrafast approximation for phylogenetic bootstrap.
 * Mol. Biol. Evol., 30:1188-1195. (free reprint, DOI: 10.1093/molbev/mst024) */
PLL_EXPORT double pllmod_opt_minimize_brent(double xmin,
                                           double xguess,
                                           double xmax,
                                           double xtol,
                                           double *fx,
                                           double *f2x,
                                           void * params,
                                           double (*target_funk)(
                                               void *,
                                               double))
{
  double eps, optx, ax, bx, cx, fa, fb, fc;
  int outbounds_ax, outbounds_cx;

  /* first attempt to bracketize minimum */
  if (xguess < xmin)
    xguess = xmin;
  if (xguess > xmax)
    xguess = xmax;
  eps = xguess * xtol * 50.0;
  ax = xguess - eps;
  outbounds_ax = ax < xmin;
  if (outbounds_ax)
    ax = xmin;
  bx = xguess;
  cx = xguess + eps;
  outbounds_cx = cx > xmax;
  if (outbounds_cx)
    cx = xmax;

  /* check if this works */
  fa = target_funk (params, ax);
  fb = target_funk (params, bx);
  fc = target_funk (params, cx);

  /* if it works use these borders else be conservative */
  if ((fa < fb) || (fc < fb))
  {
    if (!outbounds_ax)
      fa = target_funk (params, xmin);
    if (!outbounds_cx)
      fc = target_funk (params, xmax);
    ax = xmin;
    cx = xmax;
  }

  optx = brent_opt (ax, bx, cx, xtol, fx, f2x, fa, fb, fc, params,
                    target_funk);
  if (*fx > fb) // if worse, return initial value
  {
    *fx = target_funk (params, bx);
    return bx;
  }

  return optx; /* return optimal x */
}

/******************************************************************************/
/* EXPECTATION-MAXIMIZATION (EM)     */
/* Wang, Li, Susko, and Roger (2008) */
/******************************************************************************/
PLL_EXPORT void pllmod_opt_minimize_em(double *w,
                                       unsigned int w_count,
                                       double *sitecat_lh,
                                       unsigned int *site_w,
                                       unsigned int l,
                                       void * params,
                                       double (*update_sitecatlk_funk)(
                                           void *,
                                           double *))
{
  unsigned int i, c;
  unsigned int max_steps = 10;
  int converged = 0;
  int ratio_scale = 0;

  double *new_prop = (double *) malloc(sizeof(double) * w_count);
  double *ratio_prop = (double *) malloc(sizeof(double) * w_count);

  while (!converged && max_steps--)
  {
    /* update site-cat LK */
    update_sitecatlk_funk(params, sitecat_lh);

    // Expectation
    double *this_lk_cat = sitecat_lh;
    if (ratio_scale)
    {
      for (i = 0; i < l; ++i)
      {
        for (c = 0; c < w_count; c++)
        {
          this_lk_cat[c] *= ratio_prop[c];
        }
        this_lk_cat += w_count;
      }
    }
    else
      ratio_scale = 1;

    memset (new_prop, 0, w_count * sizeof(double));

    this_lk_cat = sitecat_lh;
    for (i=0; i<l; ++i)
    {
      //TODO: Check for p_invar
      double lk_ptn = 0;
      for (c = 0; c < w_count; c++)
      {
        lk_ptn += this_lk_cat[c];
      }
      lk_ptn = site_w[i] / lk_ptn;
      for (c = 0; c < w_count; c++)
      {
        new_prop[c] += this_lk_cat[c] * lk_ptn;
      }
      this_lk_cat += w_count;
    }

    // Maximization
    converged = 1;
    for (c = 0; c < w_count; c++)
    {
      new_prop[c] /= l;

      // check for convergence
      converged = converged && (fabs (w[c] - new_prop[c]) < 1e-4);
      ratio_prop[c] = new_prop[c] / w[c];
      w[c] = new_prop[c];
    }
  }

  free(ratio_prop);
  free(new_prop);
}
