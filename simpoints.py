#!/usr/bin/python3
import numpy as np
from sklearn.cluster import KMeans
import subprocess
import glob
import time
import sys
import math
import re
import argparse
import matplotlib
matplotlib.use('PDF')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages


def dist(x,y):
    s = 0.0
    for k in x:
        if k not in y:
            s = s + x[k]
        else:
            s = s + math.fabs(x[k] - y[k])
            
    for k in y:
        if k not in x:
            s = s + y[k]                        
    return s

def euclidean_dist(x,y):
    s = 0.0
    
    for i in range(0, len(x)):
        d = (x[i] - y[i])
        s = s + d*d
        
    return math.sqrt(s);

def heatmap2d(arr, name):
    pp = PdfPages(name + '.pdf')
    plt.imshow(arr, cmap='viridis')
    plt.colorbar()
    plt.savefig(pp,format='pdf',dpi=300)
    pp.close()

def make_matrix(vectors):
    max_d = 0
    for v in vectors:
        for d in v:
            if d > max_d:
                max_d = d
                
    mtx = np.zeros((len(vectors), max_d+1))
    for i in range(0, len(vectors)):
        v = vectors[i]
        for d in v:
            mtx[i][d] = v[d]
            
    return mtx;

def random_projection(in_mtx, target_dim):
    n_samples = in_mtx.shape[0]
    n_bbs = in_mtx.shape[1]
    r_mtx = 2.0 * np.random.rand(n_bbs, target_dim)
    t_mtx = in_mtx.dot(r_mtx)
    t_mtx = t_mtx - np.ones(t_mtx.shape)
    return t_mtx
    
if __name__ == '__main__':
    vectors = []
    keys = set()
    with open(sys.argv[1], 'r') as in_:
        for line in in_:
            if line[0] != 'T':
                continue
            line = line[1:len(line)]
            toks = line.split(' ')
            d = dict()
            s = 0.0
            for t in toks:
                m = re.search(r':(\d+):(\d+)', t)
                if m == None:
                    continue
                g = m.groups()
                i = int(g[0])
                if i not in keys:
                    keys.add(i)
                s = s + float(g[1])
                d[i] = float(g[1])
            #normalize
            for k in d:
                d[k] = d[k] / s                
            vectors.append(d)


    m = make_matrix(vectors)        
    r = random_projection(m, 15)

    clusters = 4
    kmeans = KMeans(n_clusters=clusters, random_state=0, n_init="auto").fit(r)
    centers = [-1] * clusters
        
    #find samples closest to centers
    for c in range(0, len(kmeans.cluster_centers_)):
        center =  kmeans.cluster_centers_[c]
        #for each sample
        min_dist = euclidean_dist(center, r[0])
        min_idx = 0
        for i in range(1, r.shape[0]):
            d = euclidean_dist(center, r[i])
            if d < min_dist:
                min_dist = d;
                min_idx = i
        centers[c] = min_idx        

        
    #compute weights using labels
    weights = [0] * clusters    
    for i in kmeans.labels_:
        weights[i] = weights[i] + 1 
    for i in range(0, clusters):
        weights[i] = weights[i] / float(kmeans.labels_.shape[0])
        
    with open('simpoints', 'w') as o:
        for c in range(0, len(centers)):
            o.write('%d %d\n' % (centers[c], c))

    with open('weights', 'w') as o:
        for w in range(0, len(weights)):
            o.write('%g %d\n' % (weights[w], w))
            
    
