import argparse
import csv

import matplotlib.pyplot as plt
import networkx as nx

parser = argparse.ArgumentParser()

parser.add_argument("--file", type=str, required=False, help="Path to the network topology - .csv file")

args = parser.parse_args()

if args.file is None:
    path = "./build/topology.csv"
else:
    path = args.file

G = nx.Graph()
with open(path) as file:
    reader = csv.reader(file)
    for row in reader:
        a = f"0x{int(row[0]):04x}"
        b = f"0x{int(row[1]):04x}"
        if a == b:
            G.add_edge("MASTER", a)
        else:
            G.add_edge(a, b)

sizes, colors = [], []
for v in G.nodes():
    if v == "MASTER":
        sizes.append(1000)
        colors.append("r")
    else:
        sizes.append(300)
        colors.append("w")

# A better layout if network starts to get big
pos = nx.kamada_kawai_layout(G)

plt.title("Topology")
nx.draw(G, pos=pos, node_size=sizes, node_color=colors, with_labels=True, font_weight="bold")
plt.show()
