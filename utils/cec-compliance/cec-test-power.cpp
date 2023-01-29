// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <cerrno>
#include <ctime>
#include <string>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

#include "cec-compliance.h"


static bool get_power_status(struct node *node, unsigned me, unsigned la, __u8 &power_status)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_give_device_power_status(&msg, true);
	msg.timeout = 2000;
	int res = doioctl(node, CEC_TRANSMIT, &msg);
	if (res == ENONET) {
		power_status = CEC_OP_POWER_STATUS_STANDBY;
		return true;
	}
	if (res || !(msg.tx_status & CEC_TX_STATUS_OK) || timed_out_or_abort(&msg))
		return false;
	cec_ops_report_power_status(&msg, &power_status);
	return true;
}

bool util_interactive_ensure_power_state(struct node *node, unsigned me, unsigned la, bool interactive,
						__u8 target_pwr)
{
	interactive_info(true, "Please ensure that the device is in state %s.",
			 power_status2s(target_pwr));

	if (!node->remote[la].has_power_status)
		return true;

	while (interactive) {
		__u8 pwr;

		if (!get_power_status(node, me, la, pwr))
			announce("Failed to retrieve power status.");
		else if (pwr == target_pwr)
			return true;
		else
			announce("The device reported power status %s.", power_status2s(pwr));
		if (!question("Retry?"))
			return false;
	}

	return true;
}


/* Give Device Power Status */

static int power_status_give(struct node *node, unsigned me, unsigned la, bool interactive)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_give_device_power_status(&msg, true);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(timed_out(&msg));
	fail_on_test(unrecognized_op(&msg));
	if (refused(&msg))
		return OK_REFUSED;
	if (cec_msg_status_is_abort(&msg))
		return OK_PRESUMED;

	__u8 power_status;
	cec_ops_report_power_status(&msg, &power_status);
	fail_on_test(power_status >= 4);

	return 0;
}

static int power_status_report(struct node *node, unsigned me, unsigned la, bool interactive)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_report_power_status(&msg, CEC_OP_POWER_STATUS_ON);
	fail_on_test(!transmit_timeout(node, &msg));
	if (unrecognized_op(&msg))
		return OK_NOT_SUPPORTED;
	if (refused(&msg))
		return OK_REFUSED;

	return OK_PRESUMED;
}

const vec_remote_subtests power_status_subtests{
	{ "Give Device Power Status", CEC_LOG_ADDR_MASK_ALL, power_status_give },
	{ "Report Device Power Status", CEC_LOG_ADDR_MASK_ALL, power_status_report },
};

/* One Touch Play */

static int one_touch_play_view_on(struct node *node, unsigned me, unsigned la, bool interactive,
				  __u8 opcode)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	if (opcode == CEC_MSG_IMAGE_VIEW_ON)
		cec_msg_image_view_on(&msg);
	else if (opcode == CEC_MSG_TEXT_VIEW_ON)
		cec_msg_text_view_on(&msg);

	int res = doioctl(node, CEC_TRANSMIT, &msg);

	if (res == ENONET && la == CEC_LOG_ADDR_TV) {
		msg.msg[0] = (CEC_LOG_ADDR_UNREGISTERED << 4) | la;
		res = doioctl(node, CEC_TRANSMIT, &msg);
	}
	fail_on_test(res || !(msg.tx_status & CEC_TX_STATUS_OK));

	fail_on_test(is_tv(la, node->remote[la].prim_type) && unrecognized_op(&msg));
	if (refused(&msg))
		return OK_REFUSED;
	if (cec_msg_status_is_abort(&msg))
		return OK_PRESUMED;
	if (opcode == CEC_MSG_IMAGE_VIEW_ON)
		node->remote[la].has_image_view_on = true;
	else if (opcode == CEC_MSG_TEXT_VIEW_ON)
		node->remote[la].has_text_view_on = true;

	return 0;
}

static int one_touch_play_image_view_on(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return one_touch_play_view_on(node, me, la, interactive, CEC_MSG_IMAGE_VIEW_ON);
}

static int one_touch_play_text_view_on(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return one_touch_play_view_on(node, me, la, interactive, CEC_MSG_TEXT_VIEW_ON);
}

static int one_touch_play_view_on_wakeup(struct node *node, unsigned me, unsigned la, bool interactive,
					 __u8 opcode)
{
	fail_on_test(!util_interactive_ensure_power_state(node, me, la, interactive, CEC_OP_POWER_STATUS_STANDBY));

	int ret = one_touch_play_view_on(node, me, la, interactive, opcode);

	if (ret && ret != OK_PRESUMED)
		return ret;
	fail_on_test(interactive && !question("Did the TV turn on?"));

	if (interactive)
		return 0;

	return OK_PRESUMED;
}

static int one_touch_play_image_view_on_wakeup(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!interactive || !node->remote[la].has_image_view_on)
		return NOTAPPLICABLE;
	return one_touch_play_view_on_wakeup(node, me, la, interactive, CEC_MSG_IMAGE_VIEW_ON);
}

static int one_touch_play_text_view_on_wakeup(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!interactive || !node->remote[la].has_text_view_on)
		return NOTAPPLICABLE;
	return one_touch_play_view_on_wakeup(node, me, la, interactive, CEC_MSG_TEXT_VIEW_ON);
}

static int one_touch_play_view_on_change(struct node *node, unsigned me, unsigned la, bool interactive,
					 __u8 opcode)
{
	struct cec_msg msg;
	int ret;

	fail_on_test(!util_interactive_ensure_power_state(node, me, la, interactive, CEC_OP_POWER_STATUS_ON));

	interactive_info(true, "Please switch the TV to another source.");
	ret = one_touch_play_view_on(node, me, la, interactive, opcode);
	if (ret && ret != OK_PRESUMED)
		return ret;
	cec_msg_init(&msg, me, la);
	cec_msg_active_source(&msg, node->phys_addr);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(interactive && !question("Did the TV switch to this source?"));

	if (interactive)
		return 0;

	return OK_PRESUMED;
}

static int one_touch_play_image_view_on_change(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!interactive || !node->remote[la].has_text_view_on)
		return NOTAPPLICABLE;
	return one_touch_play_view_on_change(node, me, la, interactive, CEC_MSG_IMAGE_VIEW_ON);
}

static int one_touch_play_text_view_on_change(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!interactive || !node->remote[la].has_text_view_on)
		return NOTAPPLICABLE;
	return one_touch_play_view_on_change(node, me, la, interactive, CEC_MSG_TEXT_VIEW_ON);
}

static int one_touch_play_req_active_source(struct node *node, unsigned me, unsigned la, bool interactive)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_active_source(&msg, node->phys_addr);
	fail_on_test(!transmit_timeout(node, &msg));

	/* We have now said that we are active source, so receiving a reply to
	   Request Active Source should fail the test. */
	cec_msg_init(&msg, me, la);
	cec_msg_request_active_source(&msg, true);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(!timed_out(&msg));

	return 0;
}

const vec_remote_subtests one_touch_play_subtests{
	{ "Image View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_image_view_on },
	{ "Text View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_text_view_on },
	{ "Wakeup on Image View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_image_view_on_wakeup },
	{ "Wakeup Text View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_text_view_on_wakeup },
	{ "Input change on Image View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_image_view_on_change },
	{ "Input change on Text View On", CEC_LOG_ADDR_MASK_TV, one_touch_play_text_view_on_change },
	{ "Active Source and Request Active Source", CEC_LOG_ADDR_MASK_ALL, one_touch_play_req_active_source },
};

/* Standby / Resume */

/* The default sleep time between power status requests. */
#define SLEEP_POLL_POWER_STATUS 2

static bool wait_changing_power_status(struct node *node, unsigned me, unsigned la, __u8 &new_status,
				       unsigned &unresponsive_cnt)
{
	__u8 old_status;
	time_t t = time(nullptr);

	announce("Checking for power status change. This may take up to %llu s.", (long long)long_timeout);
	if (!get_power_status(node, me, la, old_status))
		return false;
	while (time(nullptr) - t < long_timeout) {
		__u8 power_status;

		if (!get_power_status(node, me, la, power_status)) {
			/* Some TVs become completely unresponsive when transitioning
			   between power modes. Register that this happens, but continue
			   the test. */
			unresponsive_cnt++;
		} else if (old_status != power_status) {
			new_status = power_status;
			return true;
		}
		sleep(SLEEP_POLL_POWER_STATUS);
	}
	new_status = old_status;
	return false;
}

static bool poll_stable_power_status(struct node *node, unsigned me, unsigned la,
				     __u8 expected_status, unsigned &unresponsive_cnt)
{
	bool transient = false;
	unsigned time_to_transient = 0;
	time_t t = time(nullptr);

	/* Some devices can use several seconds to transition from one power
	   state to another, so the power state must be repeatedly polled */
	announce("Waiting for new stable power status. This may take up to %llu s.", (long long)long_timeout);
	while (time(nullptr) - t < long_timeout) {
		__u8 power_status;

		if (!get_power_status(node, me, la, power_status)) {
			/* Some TVs become completely unresponsive when transitioning
			   between power modes. Register that this happens, but continue
			   the test. */
			unresponsive_cnt++;
			sleep(SLEEP_POLL_POWER_STATUS);
			continue;
		}
		if (!transient && (power_status == CEC_OP_POWER_STATUS_TO_ON ||
				   power_status == CEC_OP_POWER_STATUS_TO_STANDBY)) {
			time_to_transient = time(nullptr) - t;
			transient = true;
			warn_once_on_test(expected_status == CEC_OP_POWER_STATUS_ON &&
					  power_status == CEC_OP_POWER_STATUS_TO_STANDBY);
			warn_once_on_test(expected_status == CEC_OP_POWER_STATUS_STANDBY &&
					  power_status == CEC_OP_POWER_STATUS_TO_ON);
		}
		if (power_status == expected_status) {
			if (transient)
				announce("Transient state after %d s, stable state %s after %d s",
					 time_to_transient, power_status2s(power_status), (int)(time(NULL) - t));
			else
				announce("No transient state reported, stable state %s after %d s",
					 power_status2s(power_status), (int)(time(NULL) - t));
			return true;
		}
		sleep(SLEEP_POLL_POWER_STATUS);
	}
	return false;
}

static int standby_resume_standby(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!node->remote[la].has_power_status)
		return NOTAPPLICABLE;

	struct cec_msg msg;
	unsigned unresponsive_cnt = 0;

	fail_on_test(!util_interactive_ensure_power_state(node, me, la, interactive, CEC_OP_POWER_STATUS_ON));

	/*
	 * Some displays only accept Standby from the Active Source.
	 * So make us the Active Source before sending Standby.
	 */
	if (is_tv(la, node->remote[la].prim_type)) {
		announce("Sending Active Source message.");
		cec_msg_init(&msg, me, la);
		cec_msg_active_source(&msg, node->phys_addr);
		fail_on_test(doioctl(node, CEC_TRANSMIT, &msg));
	}
	announce("Sending Standby message.");

	cec_msg_init(&msg, me, la);
	cec_msg_standby(&msg);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(cec_msg_status_is_abort(&msg));
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_STANDBY, unresponsive_cnt));
	fail_on_test(interactive && !question("Is the device in standby?"));
	node->remote[la].in_standby = true;

	if (unresponsive_cnt > 0)
		warn("The device went correctly into standby, but was unresponsive %d times during the transition.\n",
		     unresponsive_cnt);

	return 0;
}

static int standby_resume_standby_toggle(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!node->remote[la].in_standby)
		return NOTAPPLICABLE;

	struct cec_msg msg;
	unsigned unresponsive_cnt = 0;
	__u8 new_status;

	node->remote[la].in_standby = false;

	/* Send Standby again to test that it is not acting like a toggle */
	announce("Sending Standby message.");
	cec_msg_init(&msg, me, la);
	cec_msg_standby(&msg);
	int res = doioctl(node, CEC_TRANSMIT, &msg);
	fail_on_test(res && res != ENONET);
	fail_on_test(cec_msg_status_is_abort(&msg));
	fail_on_test(wait_changing_power_status(node, me, la, new_status, unresponsive_cnt));
	fail_on_test(new_status != CEC_OP_POWER_STATUS_STANDBY);

	if (res == ENONET) {
		struct cec_caps caps = { };
		doioctl(node, CEC_ADAP_G_CAPS, &caps);
		unsigned major = caps.version >> 16;
		unsigned minor = (caps.version >> 8) & 0xff;
		if (!strcmp(caps.driver, "pulse8-cec") &&
		    !((major == 4 && minor == 19) || major > 5 ||
		      (major == 5 && minor >= 4))) {
			// The cec framework had a bug that prevented it from reliably
			// working with displays that pull down the HPD. This was fixed
			// in commit ac479b51f3f4 for kernel 5.5 and backported to kernels
			// 4.19.94 and 5.4.9. We only warn when the pulse8-cec driver is used,
			// for other CEC devices you hopefully know what you are doing...
			warn("This display appears to pull down the HPD when in Standby. For such\n");
			warn("displays kernel 4.19 or kernel 5.4 or higher is required.\n");
		}
	}

	fail_on_test(interactive && !question("Is the device still in standby?"));
	node->remote[la].in_standby = true;
	if (unresponsive_cnt > 0)
		warn("The device went correctly into standby, but was unresponsive %d times during the transition.\n",
		     unresponsive_cnt);

	return 0;
}

static int standby_resume_active_source_nowake(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!node->remote[la].in_standby)
		return NOTAPPLICABLE;

	struct cec_msg msg;
	unsigned unresponsive_cnt = 0;
	__u8 new_status;

	node->remote[la].in_standby = false;

	/*
	 * In CEC 2.0 it is specified that a device shall not go out of standby
	 * if an Active Source message is received. The CEC 1.4 implies this as
	 * well, even though it is not as clear about this as the 2.0 spec.
	 */
	announce("Sending Active Source message.");
	cec_msg_init(&msg, me, la);
	cec_msg_active_source(&msg, node->phys_addr);
	int res = doioctl(node, CEC_TRANSMIT, &msg);
	fail_on_test(res && res != ENONET);
	fail_on_test_v2_warn(node->remote[la].cec_version,
			     wait_changing_power_status(node, me, la, new_status, unresponsive_cnt));
	fail_on_test_v2_warn(node->remote[la].cec_version,
			     new_status != CEC_OP_POWER_STATUS_STANDBY);
	if (new_status != CEC_OP_POWER_STATUS_STANDBY)
		return standby_resume_standby(node, me, la, interactive);

	node->remote[la].in_standby = true;
	if (unresponsive_cnt > 0)
		warn("The device stayed correctly in standby, but was unresponsive %d times.\n",
		     unresponsive_cnt);
	return 0;
}

static int wakeup_rc(struct node *node, unsigned me, unsigned la)
{
	struct cec_msg msg;
	struct cec_op_ui_command rc_press = {};

	/* Todo: A release should be sent after this */
	cec_msg_init(&msg, me, la);
	rc_press.ui_cmd = CEC_OP_UI_CMD_POWER_ON_FUNCTION;
	cec_msg_user_control_pressed(&msg, &rc_press);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(cec_msg_status_is_abort(&msg));

	return 0;
}

static int wakeup_tv(struct node *node, unsigned me, unsigned la)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_image_view_on(&msg);
	msg.timeout = 2000;
	int res = doioctl(node, CEC_TRANSMIT, &msg);
	if (res == ENONET && la == CEC_LOG_ADDR_TV) {
		msg.msg[0] = (CEC_LOG_ADDR_UNREGISTERED << 4) | la;
		res = doioctl(node, CEC_TRANSMIT, &msg);
	}
	fail_on_test(res || !(msg.tx_status & CEC_TX_STATUS_OK));
	if (!cec_msg_status_is_abort(&msg))
		return 0;

	cec_msg_init(&msg, me, la);
	cec_msg_text_view_on(&msg);
	fail_on_test(!transmit_timeout(node, &msg));
	if (!cec_msg_status_is_abort(&msg))
		return 0;

	return wakeup_rc(node, me, la);
}

static int wakeup_source(struct node *node, unsigned me, unsigned la)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_set_stream_path(&msg, node->remote[la].phys_addr);
	fail_on_test(!transmit_timeout(node, &msg));
	if (!cec_msg_status_is_abort(&msg))
		return 0;

	return wakeup_rc(node, me, la);
}

static int standby_resume_wakeup(struct node *node, unsigned me, unsigned la, bool interactive)
{
	if (!node->remote[la].in_standby)
		return NOTAPPLICABLE;

	int ret;

	if (is_tv(la, node->remote[la].prim_type))
		ret = wakeup_tv(node, me, la);
	else
		ret = wakeup_source(node, me, la);
	if (ret)
		return ret;

	unsigned unresponsive_cnt = 0;

	announce("Wait for device to wake up");
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));
	fail_on_test(interactive && !question("Is the device in On state?"));

	if (unresponsive_cnt > 0)
		warn("The device went correctly out of standby, but was unresponsive %d times during the transition.\n",
		     unresponsive_cnt);

	return 0;
}

static int standby_resume_wakeup_view_on(struct node *node, unsigned me, unsigned la, bool interactive, __u8 opcode)
{
	if (!is_tv(la, node->remote[la].prim_type))
		return NOTAPPLICABLE;

	unsigned unresponsive_cnt = 0;

	sleep(5);
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));

	int ret = standby_resume_standby(node, me, la, interactive);

	if (ret && opcode == CEC_MSG_TEXT_VIEW_ON) {
		ret = standby_resume_standby(node, me, la, interactive);
		if (!ret)
			warn("A STANDBY was sent right after the display reports it was powered on, but it was ignored.\n");
	}

	if (ret)
		return ret;

	sleep(6);

	ret = one_touch_play_view_on(node, me, la, interactive, opcode);

	if (ret)
		return ret;

	announce("Wait for device to wake up");
	unresponsive_cnt = 0;
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));
	fail_on_test(interactive && !question("Is the device in On state?"));

	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_active_source(&msg, node->phys_addr);
	fail_on_test(!transmit_timeout(node, &msg));

	if (unresponsive_cnt > 0)
		warn("The device went correctly out of standby, but was unresponsive %d times during the transition.\n",
		     unresponsive_cnt);

	return 0;
}

static int standby_resume_wakeup_image_view_on(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_resume_wakeup_view_on(node, me, la, interactive, CEC_MSG_IMAGE_VIEW_ON);
}

static int standby_resume_wakeup_text_view_on(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_resume_wakeup_view_on(node, me, la, interactive, CEC_MSG_TEXT_VIEW_ON);
}

/* Test CEC 2.0 Power State Transitions (see HDMI 2.1, 11.5.5) */
static int power_state_transitions(struct node *node, unsigned me, unsigned la, bool interactive)
{
	struct cec_msg msg = {};

	mode_set_follower(node);
	msg.timeout = 1000;
	doioctl(node, CEC_RECEIVE, &msg);
	cec_msg_init(&msg, me, la);
	cec_msg_standby(&msg);
	fail_on_test(!transmit_timeout(node, &msg));
	time_t start = time(nullptr);
	int res = util_receive(node, la, long_timeout * 1000, &msg, CEC_MSG_STANDBY,
			       CEC_MSG_REPORT_POWER_STATUS);
	fail_on_test(!res);
	if (res < 0) {
		warn("No Report Power Status seen when going to standby.\n");
		info("This might be due to the bug fix in commit cec935ce69fc\n");
		info("However, this was fixed in 5.5 and has been backported to LTS kernels,\n");
		info("so any kernel released after January 2020 should have this fix.\n");
		return OK_PRESUMED;
	}
	if (time(nullptr) - start > 3)
		warn("The first Report Power Status broadcast arrived > 3s after sending <Standby>\n");
	if (msg.msg[2] == CEC_OP_POWER_STATUS_STANDBY)
		return 0;
	fail_on_test(msg.msg[2] != CEC_OP_POWER_STATUS_TO_STANDBY);
	fail_on_test(util_receive(node, la, long_timeout * 1000, &msg, CEC_MSG_STANDBY,
		     CEC_MSG_REPORT_POWER_STATUS) <= 0);
	fail_on_test(msg.msg[2] != CEC_OP_POWER_STATUS_STANDBY);

	cec_msg_init(&msg, me, la);
	__u8 opcode;
	if (is_tv(la, node->remote[la].prim_type)) {
		cec_msg_image_view_on(&msg);
		opcode = msg.msg[2];

		int res = doioctl(node, CEC_TRANSMIT, &msg);

		if (res == ENONET && la == CEC_LOG_ADDR_TV) {
			msg.msg[0] = (CEC_LOG_ADDR_UNREGISTERED << 4) | la;
			fail_on_test(doioctl(node, CEC_TRANSMIT, &msg));
		}
	} else {
		cec_msg_set_stream_path(&msg, node->remote[la].phys_addr);
		opcode = msg.msg[2];
		fail_on_test(doioctl(node, CEC_TRANSMIT, &msg));
	}
	fail_on_test(!(msg.tx_status & CEC_TX_STATUS_OK));
	start = time(nullptr);
	fail_on_test(util_receive(node, la, long_timeout * 1000, &msg, opcode,
		     CEC_MSG_REPORT_POWER_STATUS) <= 0);
	if (time(nullptr) - start > 3)
		warn("The first Report Power Status broadcast arrived > 3s after sending <%s>\n",
		     opcode == CEC_MSG_IMAGE_VIEW_ON ? "Image View On" : "Set Stream Path");
	if (msg.msg[2] == CEC_OP_POWER_STATUS_ON)
		return 0;
	fail_on_test(msg.msg[2] != CEC_OP_POWER_STATUS_TO_ON);
	fail_on_test(util_receive(node, la, long_timeout * 1000, &msg, opcode,
		     CEC_MSG_REPORT_POWER_STATUS) <= 0);
	fail_on_test(msg.msg[2] != CEC_OP_POWER_STATUS_ON);

	return 0;
}

static int standby_resume_wakeup_deck(struct node *node, unsigned me, unsigned la, bool interactive, __u8 opcode)
{
	struct cec_msg msg;

	cec_msg_init(&msg, me, la);
	cec_msg_give_deck_status(&msg, true, CEC_OP_STATUS_REQ_ONCE);
	fail_on_test(!transmit_timeout(node, &msg));
	if (timed_out_or_abort(&msg))
		return OK_NOT_SUPPORTED;

	unsigned unresponsive_cnt = 0;

	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));

	int ret = standby_resume_standby(node, me, la, interactive);

	if (ret)
		return ret;

	cec_msg_init(&msg, me, la);
	if (opcode == CEC_OP_PLAY_MODE_PLAY_FWD)
		cec_msg_play(&msg, CEC_OP_PLAY_MODE_PLAY_FWD);
	else
		cec_msg_deck_control(&msg, CEC_OP_DECK_CTL_MODE_EJECT);
	fail_on_test(!transmit_timeout(node, &msg));
	fail_on_test(cec_msg_status_is_abort(&msg));

	unresponsive_cnt = 0;
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));
	fail_on_test(interactive && !question("Is the device in On state?"));

	return OK;
}

static int standby_resume_wakeup_deck_eject(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_resume_wakeup_deck(node, me, la, interactive, CEC_OP_DECK_CTL_MODE_EJECT);
}

static int standby_resume_wakeup_deck_play(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_resume_wakeup_deck(node, me, la, interactive, CEC_OP_PLAY_MODE_PLAY_FWD);
}

static int standby_record(struct node *node, unsigned me, unsigned la, bool interactive, bool active_source)
{
	struct cec_msg msg;
	__u8 rec_status;
	unsigned unresponsive_cnt = 0;

	cec_msg_init(&msg, me, la);
	cec_msg_record_on_own(&msg);
	msg.reply = CEC_MSG_RECORD_STATUS;
	fail_on_test(!transmit_timeout(node, &msg, 10000));
	if (timed_out_or_abort(&msg))
		return OK_NOT_SUPPORTED;
	cec_ops_record_status(&msg, &rec_status);
	fail_on_test(rec_status != CEC_OP_RECORD_STATUS_CUR_SRC &&
	             rec_status != CEC_OP_RECORD_STATUS_ALREADY_RECORDING);

	cec_msg_init(&msg, me, la);
	if (active_source)
		cec_msg_active_source(&msg, node->remote[la].phys_addr);
	else
		cec_msg_active_source(&msg, me);
	fail_on_test(!transmit_timeout(node, &msg));

	cec_msg_init(&msg, me, la);
	cec_msg_standby(&msg);
	fail_on_test(!transmit_timeout(node, &msg));
	/* Standby should not interrupt the recording. */
	fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));

	cec_msg_init(&msg, me, la);
	cec_msg_record_off(&msg, false);
	fail_on_test(!transmit_timeout(node, &msg));

	/* When the recording stops, recorder should standby unless it is the active source. */
	if (active_source) {
		fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_ON, unresponsive_cnt));
	} else {
		fail_on_test(!poll_stable_power_status(node, me, la, CEC_OP_POWER_STATUS_STANDBY, unresponsive_cnt));
		fail_on_test(interactive && !question("Is the device in standby?"));
		node->remote[la].in_standby = true;

		int ret = standby_resume_wakeup(node, me, la, interactive);
		if (ret)
			return ret;
		node->remote[la].in_standby = false;
	}

	return OK;
}

static int standby_record_active_source(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_record(node, me, la, interactive, true);
}

static int standby_record_inactive_source(struct node *node, unsigned me, unsigned la, bool interactive)
{
	return standby_record(node, me, la, interactive, false);
}

const vec_remote_subtests standby_resume_subtests{
	{ "Standby", CEC_LOG_ADDR_MASK_ALL, standby_resume_standby },
	{ "Repeated Standby message does not wake up", CEC_LOG_ADDR_MASK_ALL, standby_resume_standby_toggle },
	{ "Standby: Feature aborts unknown messages", CEC_LOG_ADDR_MASK_ALL, core_unknown, true },
	{ "Standby: Feature aborts Abort message", CEC_LOG_ADDR_MASK_ALL, core_abort, true },
	{ "Standby: Polling Message", CEC_LOG_ADDR_MASK_ALL, system_info_polling, true },
	{ "Standby: Give Device Power Status", CEC_LOG_ADDR_MASK_ALL, power_status_give, true },
	{ "Standby: Give Physical Address", CEC_LOG_ADDR_MASK_ALL, system_info_phys_addr, true },
	{ "Standby: Give CEC Version", CEC_LOG_ADDR_MASK_ALL, system_info_version, true },
	{ "Standby: Give Device Vendor ID", CEC_LOG_ADDR_MASK_ALL, vendor_specific_commands_id, true },
	{ "Standby: Give OSD Name", CEC_LOG_ADDR_MASK_ALL, device_osd_transfer_give, true },
	{ "Standby: Get Menu Language", CEC_LOG_ADDR_MASK_ALL, system_info_get_menu_lang, true },
	{ "Standby: Give Device Features", CEC_LOG_ADDR_MASK_ALL, system_info_give_features, true },
	{ "No wakeup on Active Source", CEC_LOG_ADDR_MASK_ALL, standby_resume_active_source_nowake },
	{ "Wake up", CEC_LOG_ADDR_MASK_ALL, standby_resume_wakeup },
	{ "Wake up TV on Image View On", CEC_LOG_ADDR_MASK_TV, standby_resume_wakeup_image_view_on },
	{ "Wake up TV on Text View On", CEC_LOG_ADDR_MASK_TV, standby_resume_wakeup_text_view_on },
	{ "Power State Transitions", CEC_LOG_ADDR_MASK_TV, power_state_transitions, false, true },
	{ "Deck Eject Standby Resume", CEC_LOG_ADDR_MASK_PLAYBACK | CEC_LOG_ADDR_MASK_RECORD, standby_resume_wakeup_deck_eject },
	{ "Deck Play Standby Resume", CEC_LOG_ADDR_MASK_PLAYBACK | CEC_LOG_ADDR_MASK_RECORD, standby_resume_wakeup_deck_play },
	{ "Record Standby Active Source", CEC_LOG_ADDR_MASK_RECORD | CEC_LOG_ADDR_MASK_BACKUP, standby_record_active_source },
	{ "Record Standby Inactive Source", CEC_LOG_ADDR_MASK_RECORD | CEC_LOG_ADDR_MASK_BACKUP, standby_record_inactive_source },
};
