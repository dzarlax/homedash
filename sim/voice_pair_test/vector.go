// Cross-language wire vector: Go signs clear, firmware verifies it; firmware
// signs proof and emits QR, Go independently reconstructs and verifies both.
package main
import (
 "bytes"
 "crypto/ecdsa"
 "crypto/elliptic"
 "crypto/sha256"
 "encoding/base64"
 "encoding/binary"
 "encoding/hex"
 "fmt"
 "math/big"
 "os"
 "os/exec"
 "strings"
)
func die(v ...any) { fmt.Fprintln(os.Stderr,v...); os.Exit(1) }
func main() {
 if len(os.Args)!=2 { die("usage: go run vector.go /path/to/vector") }
 curve:=elliptic.P256(); gx,gy:=curve.ScalarBaseMult([]byte{1}); device:=elliptic.Marshal(curve,gx,gy)
 sx,sy:=curve.ScalarBaseMult([]byte{2}); server:=elliptic.Marshal(curve,sx,sy)
 b:=[]byte("HomeDash voice server clear v1\x00\x01\x01"); bridge:=make([]byte,16); bridge[0]=1; dev:=make([]byte,16); dev[0]=2; gen:=make([]byte,16); gen[0]=3; nonce:=make([]byte,32); nonce[0]=5
 b=append(b,bridge...); b=append(b,dev...); fingerprint:=sha256.Sum256(device); b=append(b,fingerprint[:]...); b=append(b,gen...); b=binary.BigEndian.AppendUint64(b,4); b=append(b,nonce...)
 h:=sha256.Sum256(b); k:=big.NewInt(3); rx,_:=curve.ScalarBaseMult(k.Bytes()); r:=new(big.Int).Mod(rx,curve.Params().N); s:=new(big.Int).Mul(r,big.NewInt(2)); s.Add(s,new(big.Int).SetBytes(h[:])); s.Mul(s,new(big.Int).ModInverse(k,curve.Params().N)); s.Mod(s,curve.Params().N)
 half:=new(big.Int).Rsh(new(big.Int).Set(curve.Params().N),1); if s.Cmp(half)>0 {s.Sub(curve.Params().N,s)}
 signature:=make([]byte,64); r.FillBytes(signature[:32]); s.FillBytes(signature[32:])
 high:=append([]byte(nil),signature...); new(big.Int).Sub(curve.Params().N,s).FillBytes(high[32:]);
 cmd:=exec.Command(os.Args[1],hex.EncodeToString(device),hex.EncodeToString(server),hex.EncodeToString(signature),hex.EncodeToString(high)); output,err:=cmd.Output(); if err!=nil {die(err,string(output))}; lines:=strings.Split(strings.TrimSpace(string(output)),"\n"); if len(lines)!=9 {die("invalid output")}
 origin:="https://bridge.example:443"
 for op:=1; op<=3; op++ {
  expected:=[]byte("HomeDash voice device proof v1\x00\x01"); expected=append(expected,byte(op)); expected=append(expected,bridge...); expected=append(expected,dev...)
  session:=make([]byte,32); if op!=3 {session[0]=7}; challenge:=make([]byte,32); challenge[0]=8; expected=append(expected,session...); expected=append(expected,challenge...)
  proofGen:=make([]byte,16); if op!=1 {proofGen[0]=3}; expected=append(expected,proofGen...); expected=binary.BigEndian.AppendUint16(expected,uint16(len(origin))); expected=append(expected,origin...); epoch:=uint64(0); if op==3 {epoch=4}; expected=binary.BigEndian.AppendUint64(expected,epoch)
  actual,err:=hex.DecodeString(lines[(op-1)*2]); if err!=nil || !bytes.Equal(actual,expected) {die("proof transcript mismatch",op)}
  proof,err:=hex.DecodeString(lines[(op-1)*2+1]); if err!=nil || len(proof)!=64 {die("invalid proof signature")}; pr,ps:=new(big.Int).SetBytes(proof[:32]),new(big.Int).SetBytes(proof[32:]); hash:=sha256.Sum256(expected); if ps.Sign()<=0 || ps.Cmp(half)>0 || !ecdsa.Verify(&ecdsa.PublicKey{Curve:curve,X:gx,Y:gy},hash[:],pr,ps) {die("proof signature verification failed",op)}
 }
 session:=make([]byte,32); session[0]=7
 preNonce,err:=hex.DecodeString(lines[6]); if err!=nil || len(preNonce)!=32 || bytes.Equal(preNonce,make([]byte,32)) {die("invalid preauth nonce")}
 preSig,err:=hex.DecodeString(lines[7]); if err!=nil || len(preSig)!=64 {die("invalid preauth signature")}
 pre:=[]byte("HomeDash voice clear ack preauth v1\x00\x01");pre=append(pre,bridge...);pre=append(pre,dev...);pre=append(pre,gen...);pre=binary.BigEndian.AppendUint64(pre,4);pre=binary.BigEndian.AppendUint16(pre,uint16(len(origin)));pre=append(pre,origin...);pre=append(pre,preNonce...);preHash:=sha256.Sum256(pre);pres:=new(big.Int).SetBytes(preSig[32:]); if pres.Sign()<=0||pres.Cmp(half)>0||!ecdsa.Verify(&ecdsa.PublicKey{Curve:curve,X:gx,Y:gy},preHash[:],new(big.Int).SetBytes(preSig[:32]),pres) {die("preauth signature invalid")}
 originHash:=sha256.Sum256([]byte(origin))
 if !strings.HasPrefix(lines[8],"HDV2:") {die("QR prefix")}; qr,err:=base64.RawURLEncoding.Strict().DecodeString(lines[8][5:]); if err!=nil || len(qr)!=145 || qr[0]!=2 || !bytes.Equal(qr[1:17],bridge) || !bytes.Equal(qr[17:33],dev) || !bytes.Equal(qr[33:98],device) || !bytes.Equal(qr[98:130],session) || !bytes.Equal(qr[130:],originHash[:15]) {die("QR mismatch")}
 fmt.Println("Go and firmware proof, QR, and signed-clear checks passed")
}
