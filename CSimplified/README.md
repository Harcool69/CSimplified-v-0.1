# CSimplified

---

A tiny interpreted programming language written in C.

---

## Features

- Variables
- Conditions
- Printing
- Input handling
- Math expressions with parentheses (e.g. `let b be (a/100)*a`)
- Functions (`def name with a, b` ... `return` ... `end`)
- Classes with fields and methods (`class Dog` ... `end`, `new Dog`, `d.name`, `call d.bark`)
---

## Run

```bash
./src/CSimplifed your_script_path
```
If you want to compile
```bash
cd src #if you are not in src directory
gcc main.c parser.c interp.c utils.c -o the_name_you_Want_to_give_to_executable
```
---

## Example

```csimplified
print "Hello world"
```
---

## Syntax: How to learn it?

To learn syntax, see the files in the examples folder

---

Copyright © 2026 CSimplified *-by Harshil Verma*
