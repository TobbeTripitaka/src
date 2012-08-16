/* 1-2-3D finite difference modeling	*/

/*
  Copyright (C) 2011 University of Texas at Austin
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <rsf.h>
#include "wavmod.h"

int main(int argc, char* argv[])
{
	int it, jt, jtm, is, ig, st;			// index
	int nt, n1, ns, ng, m[SF_MAX_DIM], nm;				 	// dimensions
	float ot, o1, d1;				// original
	sf_file in, dat, wfl, vel, sgrid, ggrid; /* I/O files */
	float *wvlt, *wave, **data;
	sf_axis ax;
	HVel hv;
	HCoord hs, hg;
	bool verb;

	sf_init(argc,argv);

	in = sf_input ("in");   //- source wavelet
	vel  = sf_input ("vel");  //- velocity field
	sgrid  = sf_input ("sgrid");  //- source grid
	ggrid  = sf_input ("ggrid");  //- geophone grid
	dat = sf_output("out");  //- seismic data
 
	if(sf_getstring("wfl")!=NULL) 
		wfl = sf_output("wfl"); /* wavefield movie file */
	else wfl=NULL;

	ax = sf_iaxa(in, 1);
	n1 = sf_n(ax);	o1 = sf_o(ax);	d1 = sf_d(ax);

	hv = obs_vel(vel);
	m[0] = sf_n(hv->z);	nm = 1;
	if(hv->nd >= 2)	{m[1] = sf_n(hv->x); nm=2;}
	if(hv->nd >= 3)	{m[2] = sf_n(hv->y); nm=3;}
	hs = obs_coord(sgrid, m, nm);
	hg = obs_coord(ggrid, m, nm);
	ns = sf_n(hs->a2);
	ng = sf_n(hg->a2);

	if (!sf_getint("jt",&jt)) jt=1; 
	/* time interval in observation system */
	if (!sf_getint("jtm",&jtm)) jtm=100; 
	/* time interval of wave movie */
	if (!sf_getfloat("ot", &ot)) ot = o1; 
	/* time delay */
	if (!sf_getbool("verb", &verb)) verb = false; 
	/* verbosity */

	/* allocate temporary arrays */
	wvlt = sf_floatalloc(n1);
	sf_floatread(wvlt, n1, in);

	if(ot<o1) ot=o1;
	st = (ot-o1)/d1;
	nt = (n1-st)/jt+1;
	sf_setn(ax, nt);
	sf_setd(ax, d1*jt);
	sf_seto(ax, ot);
	sf_oaxa(dat, ax, 1);
	sf_oaxa(dat, hg->a2, 2);
	sf_oaxa(dat, hs->a2, 3);

	if(wfl!=NULL)
	{
		sf_oaxa(wfl, hv->z, 1);
		if(hv->nd >= 2) sf_oaxa(wfl, hv->x, 2);
		if(hv->nd >= 3) sf_oaxa(wfl, hv->y, 3);
		sf_setn(ax, n1/jtm);
		sf_setd(ax, d1*jtm);
		sf_seto(ax, ot+d1*jtm);
		sf_oaxa(wfl, ax, hv->nd+1);
		sf_oaxa(wfl, hs->a2, hv->nd+2);
	}

	data = sf_floatalloc2(nt, ng);

	wavmod_init(d1, hv);


	for (is=0; is < ns; is++)
	{
		wavmod_shot_init();
		for (it=0; it < n1; it++) 
		{
			wave = wavmod( wvlt[it], hs->p[is]);

			if(it >= st && (it-st)%jt==0)
			{
#ifdef _OPENMP
#pragma omp parallel for         \
    schedule(dynamic,8)          \
    private(ig)                  
#endif
				for(ig=0; ig<ng; ig++)
				data[ig][(it-st)/jt] = wave[hg->p[ig]];
			}
			if(wfl!=NULL && it>st && (it-st)%jtm == 0)// wave
				sf_floatwrite(wave, hv->nxyz, wfl);
			if(verb) sf_warning("%d %d of %d;", is, it, n1);
		}
		/* output seismic data */
		sf_floatwrite(data[0], nt*ng, dat);
	}
	wavmod_close();

	free(data[0]);
	free(data);
	free(wvlt);
	return (0);
}
