# Map designer for "Citadel" - 48x48 tiles of 2m. Outputs C++ string rows.
W=H=48
g=[['#']*W for _ in range(H)]
def rect(c0,r0,c1,r1,ch):
    for r in range(r0,r1+1):
        for c in range(c0,c1+1): g[r][c]=ch
def put(c,r,ch): g[r][c]=ch
# --- carve walkable
rect(17,2,30,6,'.')    # CT spawn
rect(8,3,16,6,'.')     # CT -> A
rect(31,3,39,6,'.')    # CT -> B
rect(3,6,16,16,'.')    # A site
rect(3,17,7,17,'.')
rect(31,6,44,16,'.')   # B site
rect(40,17,44,17,'.')
rect(21,7,26,12,'.')   # CT mid
rect(20,13,27,30,'.')  # mid
rect(10,18,19,22,'.')  # A connector (short)
rect(10,17,14,17,'.')
rect(28,19,37,22,'.')  # B connector
rect(33,17,37,18,'.')
rect(20,31,27,40,'.')  # lower mid
rect(5,40,42,45,'.')   # T spawn
rect(2,18,7,40,'.')    # A long
rect(40,18,45,40,'.')  # B tunnels
rect(8,38,19,40,'.')   # T -> long outside
rect(28,38,39,40,'.')  # T -> tunnels outside
# chokes
rect(2,26,3,27,'#'); rect(7,26,7,27,'#')          # long doors
rect(40,18,40,19,'#'); rect(45,18,45,19,'#')     # tunnel exit
rect(20,22,22,22,'x'); rect(25,22,27,22,'x')     # mid doors (wood)
rect(42,29,45,30,'#')                            # tunnel bend
rect(40,33,42,34,'#')
# plant zones
for r in range(8,16):
    for c in range(5,14):
        if g[r][c]=='.': g[r][c]='A'
    for c in range(34,43):
        if g[r][c]=='.': g[r][c]='B'
# spawns
for c in range(19,29): put(c,3,'D')
for c in range(10,38): put(c,43,'T')
# --- A site props
rect(8,10,9,11,'C'); put(7,11,'k'); put(10,10,'c')
put(13,8,'c'); put(13,9,'k'); put(6,14,'c'); put(5,14,'c')
rect(4,6,6,6,'w'); rect(14,16,16,16,'w')
rect(3,9,3,10,'2'); rect(3,11,3,11,'1')   # A "heaven" platform with steps
rect(15,7,16,8,'4'); put(15,9,'3'); put(15,10,'2'); put(15,11,'1')
# --- B site props
rect(38,10,39,11,'C'); put(37,10,'k'); put(40,11,'c'); put(36,14,'#')
put(42,8,'c'); put(42,9,'c'); put(34,9,'k'); rect(31,12,31,14,'x')
rect(41,15,43,15,'w')
rect(44,6,44,8,'3'); put(44,9,'2'); put(44,10,'1')
# --- mid props
put(22,16,'c'); put(25,26,'c'); put(26,26,'k'); rect(21,33,22,34,'C'); put(23,34,'c')
rect(23,18,24,18,'w'); put(20,28,'c'); put(27,14,'k')
put(25,9,'c'); rect(21,7,22,7,'w'); rect(23,11,24,12,'#')
# --- A long props
rect(6,32,7,33,'C'); put(5,33,'k'); put(3,21,'c'); put(2,22,'c'); put(6,37,'c')
# --- connectors
rect(15,20,16,21,'#'); rect(31,19,32,20,'#')
put(12,20,'c'); put(17,19,'k'); put(30,21,'c'); put(35,20,'k')
# --- tunnels
put(43,31,'c'); put(41,24,'c'); put(44,36,'k'); put(41,38,'c')
# --- spawns props
put(12,41,'c'); put(13,41,'k'); put(34,41,'c'); put(24,44,'C'); put(18,5,'c'); put(29,5,'c'); put(23,2,'k')
for r in g: assert len(r)==W
print('static const char* kCitadel[48] = {')
for r in g: print('    "'+''.join(r)+'",')
print('};')
