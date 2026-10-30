"""Generate native trust inputs from the existing pinned PEM."""
from pathlib import Path
import argparse,base64,hashlib,re
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
R=Path(__file__).resolve().parents[1]
P=R/'certificates/cacert-2026-10-20.pem'
assert hashlib.sha256(P.read_bytes()).hexdigest()=='796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe'
def tlv(b,i):
 start=i;tag=b[i];i+=1;n=b[i];i+=1
 if n&128:
  k=n&127;assert 0<k<=4;n=int.from_bytes(b[i:i+k],'big');i+=k
 assert i+n<=len(b)
 return tag,i,i+n,start
def children(b,start,end):
 while start<end:
  t,a,z,s=tlv(b,start);yield t,a,z,s;start=z
 assert start==end
def cn_of(der):
 _,a,z,_=tlv(der,0);_,a,z,_=tlv(der,a);fields=list(children(der,a,z))
 # optional explicit version precedes serial, signature, issuer, validity, subject
 idx=5 if fields[0][0]==0xa0 else 4
 _,a,z,_=fields[idx]
 for _,ra,rz,_ in children(der,a,z):
  for _,aa,az,_ in children(der,ra,rz):
   attrs=list(children(der,aa,az));ot,oa,oz,_=attrs[0];vt,va,vz,_=attrs[1]
   if der[oa:oz]==b'\x55\x04\x03':return der[va:vz]
 return b''
def arr(name,b):return 'static const unsigned char '+name+'[] = {'+','.join(str(x) for x in b)+'};\n'
blocks=re.findall(rb'-----BEGIN CERTIFICATE-----\s*(.*?)-----END CERTIFICATE-----',P.read_bytes(),re.S)
assert len(blocks)==118
out=[];rows=[];manifest=[]
for i,b in enumerate(blocks):
 der=base64.b64decode(b);cn=cn_of(der);out.append(arr(f'root_{i}',der))
 if cn:out.append(arr(f'cn_{i}',cn))
 rows.append('{root_%d,sizeof(root_%d),%s,%d}'%(i,i,f'cn_{i}' if cn else 'NULL',len(cn)))
 manifest.append(dict(bytes=len(der),cn=cn))
# CN is an overinclusive prefilter, never the trust decision. Native SDK checks full DN.
nocn=sum(not x['cn'] for x in manifest)
groups={}
for x in manifest:
 cn=x['cn'].lower()
 if cn:groups.setdefault(cn,[]).append(x)
cap=max([nocn]+[len(v)+nocn for v in groups.values()])
bytecap=max([sum(x['bytes'] for x in manifest if not x['cn'])]+[sum(x['bytes'] for x in v)+sum(x['bytes'] for x in manifest if not x['cn']) for v in groups.values()])
out.append('static const root_entry roots[] = {\n'+',\n'.join(rows)+'\n};\n')
out.append(f'#define ROOT_COUNT 118\n#define ROOT_CANDIDATE_CAP {cap}\n#define ROOT_DER_CAP {bytecap}\n')
args.output.write_text(''.join(out))
print('roots',len(manifest),'DER bytes',sum(x['bytes'] for x in manifest),'max candidates',cap,'max candidate DER bytes',bytecap,'no CN',nocn)
