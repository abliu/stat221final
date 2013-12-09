// -*- C++ -*-
// SVM with stochastic gradient
// Copyright (C) 2007- Leon Bottou

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA

#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <cmath>

#include "assert.h"
#include "vectors.h"
#include "gzstream.h"
#include "timer.h"
#include "loss.h"
#include "data.h"

using namespace std;

// ---- Loss function

// Compile with -DLOSS=xxxx to define the loss function.
// Loss functions are defined in file loss.h)
#ifndef LOSS
# define LOSS LogLoss
#endif

// ---- Bias term

// Compile with -DBIAS=[1/0] to enable/disable the bias term.
// Compile with -DREGULARIZED_BIAS=1 to enable regularization on the bias term

#ifndef BIAS
# define BIAS 1
#endif
#ifndef REGULARIZED_BIAS
# define REGULARIZED_BIAS 0
#endif

// ---- Plain stochastic gradient descent

class SvmIsgd
{
public:
  SvmIsgd(int dim, double lambda, double eta0  = 0);
  void renorm();
  double wnorm();
  double testOne(const SVector &x, double y, double *ploss, double *pnerr);
  void trainOne(const SVector &x, double y, double eta);
public:
  void train(int imin, int imax, const xvec_t &x, const yvec_t &y, const char *prefix = "");
  void test(int imin, int imax, const xvec_t &x, const yvec_t &y, const char *prefix = "");
public:
  double evaluateEta(int imin, int imax, const xvec_t &x, const yvec_t &y, double eta);
  void determineEta0(int imin, int imax, const xvec_t &x, const yvec_t &y);
private:
  double  lambda;
  double  eta0;
  FVector w;
  double  wDivisor;
  double  wBias;
  double  t;
};

/// Constructor
SvmIsgd::SvmIsgd(int dim, double lambda, double eta0)
  : lambda(lambda), eta0(eta0), 
    w(dim), wDivisor(1), wBias(0),
    t(0)
{
}

/// Renormalize the weights
void
SvmIsgd::renorm()
{
  if (wDivisor != 1.0)
    {
      w.scale(1.0 / wDivisor);
      wDivisor = 1.0;
    }
}

/// Compute the norm of the weights
double
SvmIsgd::wnorm()
{
  double norm = dot(w,w) / wDivisor / wDivisor;
#if REGULARIZED_BIAS
  norm += wBias * wBias
#endif
  return norm;
}

/// Compute the output for one example.
double
SvmIsgd::testOne(const SVector &x, double y, double *ploss, double *pnerr)
{
  double s = dot(w,x) / wDivisor + wBias;

  if (ploss)
    *ploss += LossFunction<LOSS>::VALUE.loss(s, y);
  if (pnerr)
    *pnerr += (s * y <= 0) ? 1 : 0;
  return s;
}

// computes x * x^t * w and stores the result in the last argument
void squareMult(const SVector& x, const FVector& w, SVector& out){

  assert(x.size() <= w.size());

  for(const SVector::Pair *p_i = x; p_i->i>=0; p_i++)
  {
    double out_i = 0;
    for(const SVector::Pair *p_j = x; p_j->i>=0; p_j++)
    {
      out_i += p_i->v * p_j->v * w[p_j->i];
    }
    out.set(p_i->i, out_i);
  }
}

/// Perform one iteration of the SGD algorithm with specified gains
void
SvmIsgd::trainOne(const SVector &x, double y, double eta)
{

#if BIAS == 0
#else
  #error bias not supported
#endif

#if LOSS == HINGE_LOSS

  if(1 - y * dot(x, w) / (1 + lambda * eta) < 0)
  {
    wDivisor *= (1 + lambda * eta);
  }
  else
  {
    // efficiently compute dot(x, w_{t+1})
    double x_dot_w_next = 0;
    for(const SVector::Pair *p = x; p->i>=0; p++)
    {
      double w_i = w.get(p->i) / wDivisor;
      double w_i_next = (1 / (1 + lambda * eta)) * (w_i + p->v * eta * y);
      x_dot_w_next += p->v * w_i_next;
    }

    if(1 - y * x_dot_w_next >= 0)
    {
      wDivisor *= (1 + lambda * eta);
      w.add(x, eta*y);
    }
    else
    {
      // do nothing

    }
  }

	if (wDivisor > 1e5) renorm();

#elif LOSS == SQUARED_LOSS

	// first compute the trace g
	double g = 0;
	for(const SVector::Pair *p = x; p->i>=0; p++)
	{
	  g += p->v * p->v;
	}
	g *= 2 * eta;
	g /= 1 + eta * lambda;

	w.add(x, 2 * eta * y * wDivisor);
	SVector w_sub;
	squareMult(x, w, w_sub);
	w.combine(1 / (1 + eta*lambda), w_sub, -2 * eta / ((1 + g)*(1 + eta * lambda)*(1 + eta * lambda)));

	if (wDivisor > 1e5) renorm();
#else
  #error Loss function not supported
#endif

}


/// Perform a training epoch
void 
SvmIsgd::train(int imin, int imax, const xvec_t &xp, const yvec_t &yp, const char *prefix)
{
  cout << prefix << "Training on [" << imin << ", " << imax << "]." << endl;
  assert(imin <= imax);
  assert(eta0 > 0);
  for (int i=imin; i<=imax; i++)
    {
      if(i % 10000 == 0) cout << "iteration: " << i << endl;
      double eta = eta0 / (1 + lambda * eta0 * t);
      trainOne(xp.at(i), yp.at(i), eta);
      t += 1;
    }
  cout << prefix << setprecision(6) << "wNorm=" << wnorm();
#if BIAS
  cout << " wBias=" << wBias;
#endif
  cout << endl;
}

/// Perform a test pass
void 
SvmIsgd::test(int imin, int imax, const xvec_t &xp, const yvec_t &yp, const char *prefix)
{
  cout << prefix << "Testing on [" << imin << ", " << imax << "]." << endl;
  assert(imin <= imax);
  double nerr = 0;
  double loss = 0;
  for (int i=imin; i<=imax; i++)
    testOne(xp.at(i), yp.at(i), &loss, &nerr);
  nerr = nerr / (imax - imin + 1);
  loss = loss / (imax - imin + 1);
  double cost = loss + 0.5 * lambda * wnorm();
  cout << prefix 
       << "Loss=" << setprecision(12) << loss
       << " Cost=" << setprecision(12) << cost 
       << " Misclassification=" << setprecision(4) << 100 * nerr << "%." 
       << endl;
}

/// Perform one epoch with fixed eta and return cost

double 
SvmIsgd::evaluateEta(int imin, int imax, const xvec_t &xp, const yvec_t &yp, double eta)
{
  SvmIsgd clone(*this); // take a copy of the current state
  assert(imin <= imax);
  for (int i=imin; i<=imax; i++)
    clone.trainOne(xp.at(i), yp.at(i), eta);
  double loss = 0;
  double cost = 0;
  for (int i=imin; i<=imax; i++)
    clone.testOne(xp.at(i), yp.at(i), &loss, 0);
  loss = loss / (imax - imin + 1);
  cost = loss + 0.5 * lambda * clone.wnorm();
  // cout << "Trying eta=" << eta << " yields cost " << cost << endl;
  return cost;
}

void 
SvmIsgd::determineEta0(int imin, int imax, const xvec_t &xp, const yvec_t &yp)
{
  const double factor = 2.0;
  double loEta = 1;
  double loCost = evaluateEta(imin, imax, xp, yp, loEta);
  double hiEta = loEta * factor;
  double hiCost = evaluateEta(imin, imax, xp, yp, hiEta);
  if (loCost < hiCost)
    while (loCost < hiCost)
      {
        hiEta = loEta;
        hiCost = loCost;
        loEta = hiEta / factor;
        loCost = evaluateEta(imin, imax, xp, yp, loEta);
      }
  else if (hiCost < loCost)
    while (hiCost < loCost)
      {
        loEta = hiEta;
        loCost = hiCost;
        hiEta = loEta * factor;
        hiCost = evaluateEta(imin, imax, xp, yp, hiEta);
      }
  eta0 = loEta;
  cout << "# Using eta0=" << eta0 << endl;
}


// --- Command line arguments

const char *trainfile = 0;
const char *testfile = 0;
bool normalize = true;
double lambda = 1e-5;
int epochs = 5;
int maxtrain = -1;


void
usage(const char *progname)
{
  const char *s = ::strchr(progname,'/');
  progname = (s) ? s + 1 : progname;
  cerr << "Usage: " << progname << " [options] trainfile [testfile]" << endl
       << "Options:" << endl;
#define NAM(n) "    " << setw(16) << left << n << setw(0) << ": "
#define DEF(v) " (default: " << v << ".)"
  cerr << NAM("-lambda x")
       << "Regularization parameter" << DEF(lambda) << endl
       << NAM("-epochs n")
       << "Number of training epochs" << DEF(epochs) << endl
       << NAM("-dontnormalize")
       << "Do not normalize the L2 norm of patterns." << endl
       << NAM("-maxtrain n")
       << "Restrict training set to n examples." << endl;
#undef NAM
#undef DEF
  ::exit(10);
}

void
parse(int argc, const char **argv)
{
  for (int i=1; i<argc; i++)
    {
      const char *arg = argv[i];
      if (arg[0] != '-')
        {
          if (trainfile == 0)
            trainfile = arg;
          else if (testfile == 0)
            testfile = arg;
          else
            usage(argv[0]);
        }
      else
        {
          while (arg[0] == '-') 
            arg += 1;
          string opt = arg;
          if (opt == "lambda" && i+1<argc)
            {
              lambda = atof(argv[++i]);
              assert(lambda>0 && lambda<1e4);
            }
          else if (opt == "epochs" && i+1<argc)
            {
              epochs = atoi(argv[++i]);
              assert(epochs>0 && epochs<1e6);
            }
          else if (opt == "dontnormalize")
            {
              normalize = false;
            }
          else if (opt == "maxtrain" && i+1 < argc)
            {
              maxtrain = atoi(argv[++i]);
              assert(maxtrain > 0);
            }
          else
            {
              cerr << "Option " << argv[i] << " not recognized." << endl;
              usage(argv[0]);
            }

        }
    }
  if (! trainfile)
    usage(argv[0]);
}

void 
config(const char *progname)
{
  cout << "# Running: " << progname;
  cout << " -lambda " << lambda;
  cout << " -epochs " << epochs;
  if (! normalize) cout << " -dontnormalize";
  if (maxtrain > 0) cout << " -maxtrain " << maxtrain;
  cout << endl;
#define NAME(x) #x
#define NAME2(x) NAME(x)
  cout << "# Compiled with: "
       << " -DLOSS=" << NAME2(LOSS)
       << " -DBIAS=" << BIAS
       << " -DREGULARIZED_BIAS=" << REGULARIZED_BIAS
       << endl;
}

// --- main function

int dims;
xvec_t xtrain;
yvec_t ytrain;
xvec_t xtest;
yvec_t ytest;

int main(int argc, const char **argv)
{
  parse(argc, argv);
  config(argv[0]);
  if (trainfile)
    load_datafile(trainfile, xtrain, ytrain, dims, normalize, maxtrain);
  if (testfile)
    load_datafile(testfile, xtest, ytest, dims, normalize);
  cout << "# Number of features " << dims << "." << endl;
  // prepare svm
  int imin = 0;
  int imax = xtrain.size() - 1;
  int tmin = 0;
  int tmax = xtest.size() - 1;
  SvmIsgd svm(dims, lambda);
  Timer timer;
  // determine eta0 using sample
  int smin = 0;
  int smax = imin + min(1000, imax);
  timer.start();
  svm.determineEta0(smin, smax, xtrain, ytrain);
  timer.stop();
  // train
  for(int i=0; i<epochs; i++)
    {
      cout << "--------- Epoch " << i+1 << "." << endl;
      timer.start();
      svm.train(imin, imax, xtrain, ytrain);
      timer.stop();
      cout << "Total training time " << setprecision(6) 
           << timer.elapsed() << " secs." << endl;
      svm.test(imin, imax, xtrain, ytrain, "train: ");
      if (tmax >= tmin)
        svm.test(tmin, tmax, xtest, ytest, "test:  ");
    }
  return 0;
}
