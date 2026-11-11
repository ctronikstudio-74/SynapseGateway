const { expect } = require("chai");

describe("registry stubs", function () {
  it("mints an identity and records reputation and stake", async function () {
    const [agent] = await ethers.getSigners();
    const Identity = await ethers.getContractFactory("IdentityRegistry");
    const identity = await Identity.deploy();
    await identity.mint(agent.address, "agent");
    expect(await identity.balanceOf(agent.address)).to.equal(1);

    const Reputation = await ethers.getContractFactory("ReputationRegistry");
    const reputation = await Reputation.deploy();
    await reputation.setScore(agent.address, 10000, 20, 9000, 4);
    const score = await reputation.getReputation(agent.address);
    expect(score[0]).to.equal(10000);

    const Validation = await ethers.getContractFactory("ValidationRegistry");
    const validation = await Validation.deploy();
    await validation.setStake(agent.address, 2);
    expect(await validation.getStake(agent.address)).to.equal(2);
  });
});
