import itertools

verbose = False

n = 9
t = 1
p = 2*t
k = n + p

m  = list(range(0,n))
cb = list(range(n,k))

print (m,cb)

nH = 0

for n_errors in range(1,p+1) :
    print(f"{n_errors=}")
    for parities in itertools.combinations(cb,n_errors):
        ParityConfig = list(parities)
        print(f"\n\n-------------------------- {n_errors=} num H = {nH}---------------------------\n\n")
        print(f"Parities={ParityConfig}")
                                           
        for errors in itertools.combinations(m,n_errors):
            ErrorConfig = list(errors)
            msg = list(range(0,n))
            i = 0
            for Error_location in ErrorConfig :
                msg[Error_location] = ParityConfig[i]
                i = i+1
            
            if(verbose) :
                print(f"{nH=} = errors={ErrorConfig} msg={msg}")
            nH = nH + 1

print(f"Total number of H matrices = {nH}")
    

