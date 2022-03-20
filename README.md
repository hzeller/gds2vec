GDS2VEC
=======

Convert GDS to vector format for visualization and easy conversion to other
formats.

### Build

Get libgdsii library
```
sudo apt-get install libgdsii-dev
```

then

```
make
```

### Use

```
Usage: ./gds2vec [options] command <gdsfile>
[Command]
        ps      : output postscript
        layers  : show available layers
        desc    : print description of content
[Options]
        -h         : help
        -l <layer[,layer...]> : choose layers (allowed multiple times)
        -o <file>  : output filename (otherwise: stdout)
        -s <scale> : output scale (default: 30000)
        -t <title> : Title on base-plate
```

```
./gds2vec ps -t "AND Gate" /tmp/sky130_fd_sc_hd__and4_1.gds > /tmp/layers.ps
```

If you need DXF for your laser cutter, use the makefile-rule to create it:

```
make /tmp/layers-1.dxf
```

It is useful to create some cardboard templates for alignment while putting
things together.

Templates help alignment      | Separable
------------------------------|--------------------
![](./img/make-templates.jpg) | ![](./img/disassembled.jpg)


With 4.7μm Banana               | See through
--------------------------------|--------------------
![](./img/banana-for-scale.jpg) | ![](./img/see-through.jpg)
