#!/usr/bin/env python3
"""Independent mathematical experiments, NOT a navigation/safety implementation.

Forward Eulerian HJ level sets versus continuous Zermelo/Pontryagin shooting.
Distances NM, time hours; isotropic six-knot control. Pure NumPy/SciPy lab,
so absolute timings cannot be compared with the production C++ engines.
"""
import json
import math
import time
from pathlib import Path
import numpy as np
from scipy.integrate import solve_ivp
from scipy.optimize import least_squares


def flow(kind,x,y,t):
    z=np.zeros_like(x)
    if kind=='jet':
        u=8*np.exp(-((y-6)/1.8)**2)
        return u,z,z,-2*(y-6)/1.8**2*u,z,z
    if kind=='tide':return z+7*np.sin(2*np.pi*t/12.42-1.5),z,z,z,z,z
    if kind=='eddy':
        a=.15
        return -a*y,a*(x-10),z,z-a,z+a,z
    return z,z,z,z,z,z


def pmp(kind,start,goal,rectangles):
    """dot x=c+6 p/|p|, dot p=-(Dc)^T p; unknown angle and terminal T.

    Integrate positive horizon from normalized time s in [0,1]. Costate
    magnitude is immaterial for this isotropic unconstrained control.
    Obstacles are post-screened; this smooth PMP does not model boundary arcs.
    """
    begun=time.perf_counter();best=None;work=0;converged=0
    def integrate(a,dense=False):
        nonlocal work
        angle,T=a
        def rhs(s,q):
            nonlocal work
            work+=1
            x,y,px,py=q;u,v,ux,uy,vx,vy=flow(kind,x,y,s*T)
            norm=np.hypot(px,py)
            return np.array([u+6*px/norm,v+6*py/norm,-ux*px-vx*py,-uy*px-vy*py])*T
        return solve_ivp(rhs,[0,1],[*start,math.cos(angle),math.sin(angle)],rtol=1e-7,atol=1e-8,
                         t_eval=np.linspace(0,1,1001) if dense else [1],max_step=.04)
    def residual(a):
        sol=integrate(a)
        return sol.y[:2,-1]-goal
    for angle in np.linspace(-np.pi,np.pi,13)[:-1]:
        for duration in [np.linalg.norm(goal-start)/6,8.]:
            result=least_squares(residual,[angle,duration],bounds=([-4*np.pi,.01],[4*np.pi,24]),
                                 max_nfev=60,ftol=1e-7,xtol=1e-7,gtol=1e-7)
            if np.linalg.norm(result.fun)>.002:continue
            converged+=1;sol=integrate(result.x,True);x,y=sol.y[:2]
            blocked=any(np.any((x>=a)&(x<=c)&(y>=b)&(y<=d)) for a,b,c,d in rectangles)
            if blocked:continue
            if best is None or result.x[1]<best:best=float(result.x[1])
    return dict(arrival_hours=best,seconds=time.perf_counter()-begun,rhs_calls=work,
                converged_shootings=converged)


def levelset(kind,start,goal,rectangles,n):
    """Monotone first-order Godunov isotropic + upwind-current HJ solver.

    phi_t + c.grad(phi) + 6|grad(phi)| = 0. Fixed obstacle cells cannot become
    reached. Seed radius=0.75 NM, explicitly reported (finite source extent).
    Controls have been analytically maximized, no heading fan is propagated.
    An obstacle-free isotropic model is intentionally used to isolate PDE
    costs; polars, switching and resource states would enlarge this model.
    """
    begun=time.perf_counter();x=np.linspace(-4,24,n);y=np.linspace(-12,12,n)
    dx=x[1]-x[0];dy=y[1]-y[0];X,Y=np.meshgrid(x,y)
    phi=np.hypot(X-start[0],Y-start[1])-.75
    mask=np.zeros_like(X,dtype=bool)
    for a,b,c,d in rectangles:mask|=(X>=a)&(X<=c)&(Y>=b)&(Y<=d)
    phi[mask]=1000
    target=(int(round((goal[1]-y[0])/dy)),int(round((goal[0]-x[0])/dx)))
    t=0.;steps=0;arrival=None
    while t<12:
        u,v,*_=flow(kind,X,Y,t)
        dt=min(.45/((6+np.max(np.abs(u)))/dx+(6+np.max(np.abs(v)))/dy),12-t)
        pad=np.pad(phi,1,mode='edge')
        xm=(phi-pad[1:-1,:-2])/dx;xp=(pad[1:-1,2:]-phi)/dx
        ym=(phi-pad[:-2,1:-1])/dy;yp=(pad[2:,1:-1]-phi)/dy
        norm=np.sqrt(np.maximum(np.maximum(xm,0)**2,np.minimum(xp,0)**2)+
                     np.maximum(np.maximum(ym,0)**2,np.minimum(yp,0)**2))
        h=6*norm+np.maximum(u,0)*xm+np.minimum(u,0)*xp+np.maximum(v,0)*ym+np.minimum(v,0)*yp
        phi-=dt*h;phi[mask]=1000;t+=dt;steps+=1
        if phi[target]<=0:arrival=t;break
    return dict(arrival_hours=arrival,seconds=time.perf_counter()-begun,steps=steps,
                cells=n*n,seed_radius_nm=.75,
                working_array_lower_bound_bytes=10*n*n*8)


def main():
    start=np.array([0.,0.]);goal=np.array([20.,0.]);rows=[]
    for kind,obstacles in [('still',[]),('jet',[]),('tide',[]),('eddy',[]),
                            ('still',[(8,-4,12,4)]),
                            ('still',[(8,-12,12,1.99),(8,2.01,12,12)])]:
        case=kind+('_narrow' if len(obstacles)==2 else '_island' if obstacles else '')
        print(case,flush=True)
        rows.append(dict(case=case,pmp=pmp(kind,start,goal,obstacles),
                         hj={str(n):levelset(kind,start,goal,obstacles,n) for n in [64,128,256]}))
    path=Path(__file__).with_name('formulation-results.json');path.write_text(json.dumps(rows,indent=2)+'\n')
    print(json.dumps(rows,indent=2))


if __name__=='__main__':main()
