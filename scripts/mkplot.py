#!/usr/bin/env python2.7

from optparse import OptionParser
import itertools
import math
import matplotlib
matplotlib.use('cairo')
import matplotlib.pyplot as plt
import numpy as np


def plot(filenames, datasets=None, out='timings.png', title=None, labels=None):
    fig, axs = plt.subplots(nrows=1, ncols=2)
    fig.set_size_inches(12, 6)
    if title:
        fig.suptitle(title)
    ax = axs[0]
    ax.set_xlabel('Thread count')
    ax.set_ylabel('Elapsed time, seconds')
    axs[1].set_xlabel('Thread count')
    axs[1].set_ylabel('CPU time, seconds')

    if datasets is None:
        datasets = [np.loadtxt(f, delimiter=';') for f in filenames]

    xmax = max(dataset[:, 0].max() for dataset in datasets)*1.1
    ax.set_xlim(xmin=1, xmax=xmax)
    axs[1].set_xlim(xmin=1, xmax=xmax)

    for dataset, name in zip(datasets, labels):
        ax.plot(dataset[:, 0], dataset[:, 2], marker='o', label=name)
    ax.grid(True)
    ax.legend(loc='best')


    for dataset, name in zip(datasets, labels):
        axs[1].loglog(dataset[:, 0], dataset[:, 3] + dataset[:, 4],
                      marker='o', label='total, %s' % name)

    for idx, metric in {4: 'system'}.items():
        for dataset, name in zip(datasets, labels):
            axs[1].loglog(dataset[:, 0], dataset[:, idx], marker='o',
                          label='%s, %s' % (metric, name))
        
    axs[1].set_xticks(datasets[0][:, 0])
    axs[1].set_xticklabels([int(x) for x in datasets[0][:, 0]])
    axs[1].grid(True)
    axs[1].legend(loc='best')

    plt.savefig(out)


def main():
    parser = OptionParser()
    parser.add_option('-o', dest='plot', default='falsesharing.png')
    parser.add_option('-t', dest='title', default='False sharing demo')
    parser.add_option('-l', dest='labels', default='before;after')

    options, args = parser.parse_args()
    plot(args, out=options.plot, title=options.title,
         labels=options.labels.split(';'))


if __name__ == '__main__':
    main()
 
