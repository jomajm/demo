import random

a = 2000
s = str(a) + " "
for i in range(0,a):
    s = s + str(random.randint(1, 1000000)) + " "
s = s.rstrip()
print s
