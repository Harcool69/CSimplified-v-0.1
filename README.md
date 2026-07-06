# CSimplified

---

A tiny interpreted programming language written in C.

---

## Features

- Variables
- Python-style assignment (`x = 10`)
- Decimal numbers
- Lists, dictionaries, tuples, and sets
- Collection editing (`items[0] = 99`, `add 4 to items`, `remove 4 from items`)
- Increment and augmented assignment (`count++`, `count += 2`)
- Type casting (`int()`, `float()`, `number()`, `str()`)
- Small built-in library helpers (`input()`, `len()`, `type()`, `abs()`, `round()`)
- Conditions
- Printing
- Input handling

---

## Build

```bash
make
```
---

## Run

```bash
./bin/csimplified examples/hello.csimpl
```
---

## Example

```csimplified
print "Hello world"
```

```csimplified
price = 99.50
gst = 18.5
final_price = price + ((price * gst) / 100)
print final_price

items = [1, 2.5, "three"]
items[1] = 4.75
append 6 to items
print items

count = 1
count++
print int(final_price)
```
---

## Syntax: How to learn it?
**To learn syntax, see the files in the examples folder**
---

Copyright © 2026 CSimplified *-by Harshil Verma*
