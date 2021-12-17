# wasm-taint-attempt

### Steps to run:

#### 1. Clone the repository for node js 
	"git clone git@github.com:nodejs/node.git"

#### 2. Navigate to v8 src file
	"cd node/deps/v8"
	
#### 3. Delete the src file
	"rm -rf src"
	
#### 4. Clone this repository in its place
	"git clone git@github.com:Sean-Mangan/wasm-taint-attempt.git"
	
#### 5. Navigate to main node folder
	"cd ../.."
	
#### 6. Configure node
	"sudo ./configure"
	
#### 7. Build Node
	"sudo make -j4"
